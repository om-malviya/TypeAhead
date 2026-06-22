#include "Engine.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "Util.h"

namespace ta {

using nlohmann::json;

Engine::Engine(const Config& cfg)
    : cfg_(cfg),
      scorer_(cfg_),
      store_(cfg_.pg_conn),
      cache_(cfg_.redis_nodes, cfg_.vnodes_per_node),
      recency_mode_(cfg_.ranking_mode == "recency") {
  batch_ = std::make_unique<BatchWriter>(
      cfg_.batch_size, cfg_.flush_interval_ms,
      [this](BatchWriter::Buffer& b) { applyFlush(b); });
}

Engine::~Engine() {
  if (batch_) batch_->stop();
}

void Engine::start() {
  store_.ensureSchema();
  auto all = store_.loadAll();
  {
    std::unique_lock<std::shared_mutex> lk(rw_);
    for (auto& e : all) {
      auto p = std::make_unique<Entry>(std::move(e));
      Entry* ptr = p.get();
      const std::string key = ptr->query;
      trie_.insert(key, ptr);
      entries_[key] = std::move(p);
    }
  }
  batch_->start();
  std::fprintf(stderr,
               "[engine] loaded %zu queries | cache: %zu redis nodes, "
               "%zu virtual ring nodes | ranking=%s\n",
               entries_.size(), cache_.nodeCount(), cache_.ringSize(),
               modeStr().c_str());
}

std::string Engine::serialize(const std::vector<Suggestion>& v) const {
  json arr = json::array();
  for (const auto& s : v) {
    arr.push_back({{"query", s.query},
                   {"score", s.score},
                   {"total_count", s.total_count},
                   {"recent_count", s.recent_count}});
  }
  return arr.dump();
}

std::vector<Suggestion> Engine::deserialize(const std::string& s) const {
  std::vector<Suggestion> out;
  json j = json::parse(s, nullptr, /*allow_exceptions=*/false);
  if (!j.is_array()) return out;
  for (const auto& it : j) {
    Suggestion sg;
    sg.query        = it.value("query", std::string());
    sg.score        = it.value("score", 0.0);
    sg.total_count  = it.value("total_count", (long long)0);
    sg.recent_count = it.value("recent_count", 0.0);
    out.push_back(std::move(sg));
  }
  return out;
}

namespace {
bool betterSuggestion(const Suggestion& a, const Suggestion& b) {
  if (a.score != b.score) return a.score > b.score;
  if (a.total_count != b.total_count) return a.total_count > b.total_count;
  return a.query < b.query;  // deterministic tie-break
}
}  // namespace

std::vector<Suggestion> Engine::suggest(const std::string& prefix) {
  auto t0 = std::chrono::steady_clock::now();
  const std::string norm = normalize(prefix);
  if (norm.empty()) return {};  // graceful: empty/whitespace prefix -> no rows

  metrics_.suggest_requests++;
  const std::string key = cacheKey(norm);

  // [1] cache
  if (auto cached = cache_.get(key)) {
    metrics_.cache_hits++;
    auto res = deserialize(*cached);
    auto dt = std::chrono::steady_clock::now() - t0;
    metrics_.recordSuggestLatency(
        std::chrono::duration<double, std::milli>(dt).count());
    return res;
  }
  metrics_.cache_misses++;

  // [2] compute from the in-memory index (derived from the primary store)
  const int64_t now = nowMs();
  const bool recency = recency_mode_.load();
  std::vector<Suggestion> res;
  {
    std::shared_lock<std::shared_mutex> lk(rw_);
    auto ptrs = trie_.collect(norm);
    res.reserve(ptrs.size());
    for (Entry* e : ptrs) {
      res.push_back({e->query, scorer_.score(*e, now, recency), e->total_count,
                     e->recent_count});
    }
  }
  metrics_.db_reads++;

  const size_t limit = static_cast<size_t>(cfg_.suggest_limit);
  if (res.size() > limit) {
    std::partial_sort(res.begin(), res.begin() + limit, res.end(),
                      betterSuggestion);
    res.resize(limit);
  } else {
    std::sort(res.begin(), res.end(), betterSuggestion);
  }

  // populate cache with a TTL so it can never go permanently stale
  cache_.set(key, serialize(res), cfg_.cache_ttl_sec);

  auto dt = std::chrono::steady_clock::now() - t0;
  metrics_.recordSuggestLatency(
      std::chrono::duration<double, std::milli>(dt).count());
  return res;
}

void Engine::recordSearch(const std::string& query) {
  const std::string norm = normalize(query);
  if (norm.empty()) return;
  metrics_.searches_received++;
  batch_->push(norm);
}

std::vector<Suggestion> Engine::trending(int k) {
  const int64_t now = nowMs();
  std::vector<Suggestion> res;
  {
    std::shared_lock<std::shared_mutex> lk(rw_);
    for (const auto& [q, ptr] : entries_) {
      double d = scorer_.decayedRecent(*ptr, now);
      if (d > 0.0)
        res.push_back({ptr->query, d, ptr->total_count, ptr->recent_count});
    }
    // Fresh start (no recent activity yet): fall back to all-time popularity
    // so the trending section is never empty.
    if (res.empty()) {
      for (const auto& [q, ptr] : entries_)
        res.push_back({ptr->query, (double)ptr->total_count, ptr->total_count,
                       ptr->recent_count});
    }
  }
  const size_t kk = static_cast<size_t>(std::max(0, k));
  if (res.size() > kk) {
    std::partial_sort(res.begin(), res.begin() + kk, res.end(),
                      betterSuggestion);
    res.resize(kk);
  } else {
    std::sort(res.begin(), res.end(), betterSuggestion);
  }
  return res;
}

Engine::CacheDebug Engine::cacheDebug(const std::string& prefix) {
  CacheDebug d;
  d.prefix    = normalize(prefix);
  d.cache_key = cacheKey(d.prefix);
  auto r      = cache_.route(d.cache_key);
  d.node_name  = r.node_name;
  d.node_index = r.node_index;
  d.vnode_hash = r.vnode_hash;
  if (auto cached = cache_.get(d.cache_key)) {
    d.hit          = true;
    d.cached_count = static_cast<int>(deserialize(*cached).size());
  }
  std::fprintf(stderr, "[cache/debug] prefix=\"%s\" key=\"%s\" -> %s (vnode=%u) %s\n",
               d.prefix.c_str(), d.cache_key.c_str(), d.node_name.c_str(),
               d.vnode_hash, d.hit ? "HIT" : "MISS");
  return d;
}

Engine::EngineStats Engine::stats() {
  EngineStats s{};
  s.cache_hits          = metrics_.cache_hits.load();
  s.cache_misses        = metrics_.cache_misses.load();
  s.cache_hit_rate      = metrics_.cacheHitRate();
  s.db_reads            = metrics_.db_reads.load();
  s.db_write_statements = metrics_.db_write_statements.load();
  s.rows_written        = metrics_.rows_written.load();
  s.flush_ops           = metrics_.flush_ops.load();
  s.searches_received   = metrics_.searches_received.load();
  s.suggest_requests    = metrics_.suggest_requests.load();
  s.write_reduction_ratio =
      (double)s.searches_received / (double)std::max<long long>(1, s.db_write_statements);
  s.suggest_avg_ms = metrics_.average();
  s.suggest_p95_ms = metrics_.percentile(95.0);
  {
    std::shared_lock<std::shared_mutex> lk(rw_);
    s.num_queries = entries_.size();
  }
  s.redis_nodes   = cache_.nodeCount();
  s.ring_vnodes   = cache_.ringSize();
  s.pending_batch = batch_->pending();
  s.ranking_mode  = modeStr();
  return s;
}

void Engine::setRankingMode(bool recency) {
  recency_mode_.store(recency);
  cache_.flushAll();  // cached results were ranked under the old mode
  std::fprintf(stderr, "[engine] ranking mode -> %s (cache flushed)\n",
               modeStr().c_str());
}

Entry* Engine::getOrCreate(const std::string& q) {
  auto it = entries_.find(q);
  if (it != entries_.end()) return it->second.get();
  auto p = std::make_unique<Entry>();
  p->query = q;
  Entry* ptr = p.get();
  entries_[q] = std::move(p);
  trie_.insert(q, ptr);
  return ptr;
}

void Engine::applyFlush(BatchWriter::Buffer& batch) {
  const int64_t now = nowMs();
  std::vector<PostgresStore::UpsertRow> rows;
  rows.reserve(batch.size());
  {
    std::unique_lock<std::shared_mutex> lk(rw_);
    for (const auto& [q, delta] : batch) {
      Entry* e = getOrCreate(q);
      double decayed = scorer_.decayedRecent(*e, now);  // decay then add
      e->recent_count = decayed + static_cast<double>(delta);
      e->total_count += delta;
      e->last_searched_ms = now;
      rows.push_back({q, delta, e->recent_count, now});
    }
  }

  size_t stmts = store_.bulkUpsert(rows);  // the batch-write: 1 stmt / chunk
  metrics_.flush_ops++;
  metrics_.db_write_statements += stmts;
  metrics_.rows_written += rows.size();

  // Invalidate cache entries whose ranking just changed.
  const std::string mode = modeStr();
  for (const auto& [q, delta] : batch) {
    int maxp = std::min((int)q.size(), cfg_.max_invalidate_prefix);
    for (int k = 1; k <= maxp; ++k)
      cache_.del("sug:" + mode + ":" + q.substr(0, k));
  }

  std::fprintf(stderr,
               "[batch] flushed %zu unique queries in %zu SQL statement(s)\n",
               rows.size(), stmts);
}

}  // namespace ta
