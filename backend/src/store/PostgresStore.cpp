#include "store/PostgresStore.h"

#include <sstream>

namespace ta {

PostgresStore::PostgresStore(const std::string& conninfo) {
  conn_ = std::make_unique<pqxx::connection>(conninfo);
}

void PostgresStore::ensureSchema() {
  pqxx::work txn(*conn_);
  txn.exec(
      "CREATE TABLE IF NOT EXISTS queries ("
      "  query         TEXT PRIMARY KEY,"
      "  total_count   BIGINT           NOT NULL DEFAULT 0,"
      "  recent_count  DOUBLE PRECISION NOT NULL DEFAULT 0,"
      "  last_searched TIMESTAMPTZ"
      ")");
  // text_pattern_ops makes LIKE 'prefix%' index-usable (fallback path / loader).
  txn.exec(
      "CREATE INDEX IF NOT EXISTS idx_queries_prefix "
      "ON queries (query text_pattern_ops)");
  txn.commit();
}

std::vector<Entry> PostgresStore::loadAll() {
  std::vector<Entry> out;
  pqxx::work txn(*conn_);
  pqxx::result r = txn.exec(
      "SELECT query, total_count, recent_count, "
      "COALESCE((EXTRACT(EPOCH FROM last_searched)*1000)::bigint, 0) "
      "FROM queries");
  txn.commit();
  out.reserve(r.size());
  for (const auto& row : r) {
    Entry e;
    e.query            = row[0].as<std::string>();
    e.total_count      = row[1].as<long long>();
    e.recent_count     = row[2].as<double>();
    e.last_searched_ms = row[3].as<long long>();
    out.push_back(std::move(e));
  }
  return out;
}

namespace {
std::string fmtDouble(double v) {
  std::ostringstream os;
  os.precision(10);
  os << v;
  return os.str();
}
}  // namespace

size_t PostgresStore::bulkUpsert(const std::vector<UpsertRow>& rows) {
  if (rows.empty()) return 0;
  constexpr size_t kChunk = 1000;
  size_t statements = 0;
  for (size_t off = 0; off < rows.size(); off += kChunk) {
    size_t end = std::min(off + kChunk, rows.size());
    pqxx::work txn(*conn_);
    std::ostringstream sql;
    sql << "INSERT INTO queries(query,total_count,recent_count,last_searched) "
           "VALUES ";
    for (size_t i = off; i < end; ++i) {
      const auto& r = rows[i];
      if (i > off) sql << ',';
      sql << '(' << txn.quote(r.query) << ',' << r.total_delta << ','
          << fmtDouble(r.recent_value) << ",to_timestamp("
          << fmtDouble(r.last_searched_ms / 1000.0) << "))";
    }
    sql << " ON CONFLICT(query) DO UPDATE SET "
           "total_count = queries.total_count + EXCLUDED.total_count,"
           "recent_count = EXCLUDED.recent_count,"
           "last_searched = EXCLUDED.last_searched";
    txn.exec(sql.str());
    txn.commit();
    ++statements;
  }
  return statements;
}

size_t PostgresStore::bulkLoadCounts(
    const std::vector<std::pair<std::string, long long>>& rows) {
  if (rows.empty()) return 0;
  constexpr size_t kChunk = 1000;
  size_t statements = 0;
  for (size_t off = 0; off < rows.size(); off += kChunk) {
    size_t end = std::min(off + kChunk, rows.size());
    pqxx::work txn(*conn_);
    std::ostringstream sql;
    sql << "INSERT INTO queries(query,total_count) VALUES ";
    for (size_t i = off; i < end; ++i) {
      if (i > off) sql << ',';
      sql << '(' << txn.quote(rows[i].first) << ',' << rows[i].second << ')';
    }
    sql << " ON CONFLICT(query) DO UPDATE SET total_count = EXCLUDED.total_count";
    txn.exec(sql.str());
    txn.commit();
    ++statements;
  }
  return statements;
}

long long PostgresStore::rowCount() {
  pqxx::work txn(*conn_);
  auto r = txn.exec("SELECT COUNT(*) FROM queries");
  txn.commit();
  return r[0][0].as<long long>();
}

}  // namespace ta
