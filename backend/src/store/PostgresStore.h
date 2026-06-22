#pragma once
#include <pqxx/pqxx>

#include <memory>
#include <string>
#include <vector>

#include "Types.h"

namespace ta {

// Durable primary store: the source of truth for query counts. Survives
// restarts; the in-memory Trie/map is rebuilt from here on startup.
//
// A single connection is held and used by one thread at a time (startup
// load, then the single batch-writer thread), which matches libpqxx's
// non-thread-safe connection model.
class PostgresStore {
 public:
  // One row to persist on a batch flush.
  struct UpsertRow {
    std::string query;
    long long   total_delta;     // increment to add to total_count
    double      recent_value;    // new (already-decayed) recent_count
    int64_t     last_searched_ms;
  };

  explicit PostgresStore(const std::string& conninfo);

  void ensureSchema();

  // Load the whole table into memory (startup).
  std::vector<Entry> loadAll();

  // Persist a batch as ONE SQL statement (the batch-write win). Returns the
  // number of rows in the statement.
  size_t bulkUpsert(const std::vector<UpsertRow>& rows);

  // Used by the loader tool: overwrite total_count for ingested rows.
  size_t bulkLoadCounts(const std::vector<std::pair<std::string, long long>>& rows);

  long long rowCount();

 private:
  std::unique_ptr<pqxx::connection> conn_;
};

}  // namespace ta
