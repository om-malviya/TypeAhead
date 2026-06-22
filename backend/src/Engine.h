#pragma once
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Config.h"
#include "Types.h"
#include "batch/BatchWriter.h"
#include "cache/RedisCache.h"
#include "metrics/Metrics.h"
#include "ranking/Scorer.h"
#include "store/PostgresStore.h"
#include "core/Trie.h"

namespace ta {

// Ties together the trie, cache, store, batch writer and metrics.
// suggest(): cache hit -> return; miss -> trie + rank + cache set.
// recordSearch(): push to batch writer, which flushes to Postgres and
// invalidates affected cache keys.
class Engine {
 public:
  struct CacheDebug {
    std::string prefix;
    std::string cache_key;
    std::string node_name;
    int         node_index = -1;
    uint32_t    vnode_hash = 0;
    bool        hit = false;
    int         cached_count = 0;
  };

  struct EngineStats {
    long long cache_hits, cache_misses;
    double    cache_hit_rate;
    long long db_reads, db_write_statements, rows_written, flush_ops;
    long long searches_received, suggest_requests;
    double    write_reduction_ratio;  // searches per SQL write statement
    double    suggest_avg_ms, suggest_p95_ms;
    size_t    num_queries, redis_nodes, ring_vnodes, pending_batch;
    std::string ranking_mode;
  };

  explicit Engine(const Config& cfg);
  ~Engine();

  void start(); // load from Postgres, build trie, start batch writer

  std::vector<Suggestion> suggest(const std::string& prefix);
  void                    recordSearch(const std::string& query);
  std::vector<Suggestion> trending(int k);
  CacheDebug              cacheDebug(const std::string& prefix);
  EngineStats             stats();
  void                    setRankingMode(bool recency);  // toggles + clears cache

  const Config& config() const { return cfg_; }

 private:
  std::string modeStr() const { return recency_mode_.load() ? "recency" : "count"; }
  std::string cacheKey(const std::string& prefix) const {
    return "sug:" + modeStr() + ":" + prefix;
  }
  Entry* getOrCreate(const std::string& q); // caller must hold unique lock
  void   applyFlush(BatchWriter::Buffer& batch); // called from batch writer thread
  std::string serialize(const std::vector<Suggestion>&) const;
  std::vector<Suggestion> deserialize(const std::string&) const;

  Config        cfg_;
  Metrics       metrics_;
  Scorer        scorer_;
  PostgresStore store_;
  RedisCache    cache_;
  Trie          trie_;
  std::unordered_map<std::string, std::unique_ptr<Entry>> entries_;
  mutable std::shared_mutex rw_;  // guards trie_ + entries_
  std::atomic<bool> recency_mode_;
  std::unique_ptr<BatchWriter> batch_;
};

}  // namespace ta
