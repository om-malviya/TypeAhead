#pragma once
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>

namespace ta {

struct RedisNode {
  std::string name;
  std::string host;
  int         port;
};

namespace detail {
inline std::string envOr(const char* k, const std::string& d) {
  const char* v = std::getenv(k);
  return v ? std::string(v) : d;
}
inline int envInt(const char* k, int d) {
  const char* v = std::getenv(k);
  return v ? std::atoi(v) : d;
}
inline double envDbl(const char* k, double d) {
  const char* v = std::getenv(k);
  return v ? std::atof(v) : d;
}
}  // namespace detail

struct Config {
  // Primary store
  std::string pg_conn = "host=127.0.0.1 port=5432 dbname=typeahead";

  // Distributed cache (logical nodes)
  std::vector<RedisNode> redis_nodes;
  int vnodes_per_node = 150;  // virtual nodes per physical node on the ring
  int cache_ttl_sec   = 60;   // suggestion entries expire so stale data clears

  // Server
  int http_port = 8080;
  std::string frontend_dir;  // optional: serve built SPA from here

  // Batch writes
  int batch_size       = 500;
  int flush_interval_ms = 1000;

  // Ranking
  std::string ranking_mode      = "recency";  // "count" (basic) | "recency" (enhanced)
  double      w_total           = 1.0;
  double      w_recent          = 2.0;
  double      decay_half_life_sec = 3600.0;  // recent activity half-life
  int         suggest_limit     = 10;
  int         max_invalidate_prefix = 12;  // cap on prefix lengths invalidated per write

  static Config fromEnv() {
    using namespace detail;
    Config c;
    c.pg_conn           = envOr("TA_PG_CONN", c.pg_conn);
    c.vnodes_per_node   = envInt("TA_VNODES", c.vnodes_per_node);
    c.cache_ttl_sec     = envInt("TA_CACHE_TTL", c.cache_ttl_sec);
    c.http_port         = envInt("TA_PORT", c.http_port);
    c.frontend_dir      = envOr("TA_FRONTEND_DIR", c.frontend_dir);
    c.batch_size        = envInt("TA_BATCH_SIZE", c.batch_size);
    c.flush_interval_ms = envInt("TA_FLUSH_MS", c.flush_interval_ms);
    c.ranking_mode      = envOr("TA_RANKING", c.ranking_mode);
    c.w_total           = envDbl("TA_W_TOTAL", c.w_total);
    c.w_recent          = envDbl("TA_W_RECENT", c.w_recent);
    c.decay_half_life_sec = envDbl("TA_HALF_LIFE", c.decay_half_life_sec);
    c.suggest_limit     = envInt("TA_LIMIT", c.suggest_limit);

    std::string nodes = envOr("TA_REDIS_NODES",
                              "127.0.0.1:6379,127.0.0.1:6380,127.0.0.1:6381");
    std::stringstream ss(nodes);
    std::string item;
    int idx = 0;
    while (std::getline(ss, item, ',')) {
      if (item.empty()) continue;
      auto colon = item.find(':');
      RedisNode n;
      n.host = item.substr(0, colon);
      n.port = std::atoi(item.substr(colon + 1).c_str());
      n.name = "redis-" + std::to_string(idx++);
      c.redis_nodes.push_back(n);
    }
    return c;
  }
};

}  // namespace ta
