// Bulk-loads a `query,count` CSV into the Postgres `queries` table.
//
// Usage: load_dataset [path/to/queries.csv]   (default: data/queries.csv)
// Connection comes from TA_PG_CONN (same as the server).
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>

#include "Config.h"
#include "Util.h"
#include "store/PostgresStore.h"

using namespace ta;

int main(int argc, char** argv) {
  std::string path = argc > 1 ? argv[1] : "data/queries.csv";
  Config cfg = Config::fromEnv();

  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return 1;
  }

  // Aggregate by normalized query (duplicates after lowercasing are summed).
  std::unordered_map<std::string, long long> counts;
  std::string line;
  bool first = true;
  long long parsed = 0;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (first) {
      first = false;
      // Skip an optional header row.
      if (line.rfind("query,", 0) == 0 || line.rfind("query\t", 0) == 0) continue;
    }
    // count is the final comma-separated field; query is everything before.
    auto pos = line.rfind(',');
    if (pos == std::string::npos) continue;
    std::string q = line.substr(0, pos);
    if (q.size() >= 2 && q.front() == '"' && q.back() == '"')
      q = q.substr(1, q.size() - 2);
    long long c = 0;
    try {
      c = std::stoll(line.substr(pos + 1));
    } catch (...) {
      continue;
    }
    if (c <= 0) continue;
    q = normalize(q);
    if (q.empty()) continue;
    counts[q] += c;
    ++parsed;
  }
  std::fprintf(stderr, "parsed %lld rows -> %zu unique queries\n", parsed,
               counts.size());

  PostgresStore store(cfg.pg_conn);
  store.ensureSchema();

  std::vector<std::pair<std::string, long long>> rows(counts.begin(),
                                                      counts.end());
  size_t stmts = store.bulkLoadCounts(rows);
  std::fprintf(stderr, "loaded %zu queries in %zu SQL statement(s); table now has %lld rows\n",
               rows.size(), stmts, store.rowCount());
  return 0;
}
