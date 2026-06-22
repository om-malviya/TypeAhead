#pragma once
#include <string>
#include <cstdint>

namespace ta {

// One query and its popularity state. This is the in-memory mirror of a row
// in the Postgres `queries` table and is the unit the Trie indexes.
struct Entry {
  std::string query;
  long long   total_count = 0;    // all-time popularity (durable)
  double      recent_count = 0.0; // time-decayed recent activity (trending)
  int64_t     last_searched_ms = 0;
};

// A single ranked suggestion returned by /suggest.
struct Suggestion {
  std::string query;
  double      score = 0.0;
  long long   total_count = 0;
  double      recent_count = 0.0;
};

}  // namespace ta
