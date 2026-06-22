#pragma once
#include <hiredis/hiredis.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Config.h"
#include "core/ConsistentHashRing.h"

namespace ta {

// Distributed cache spread over several Redis nodes. The node that owns a
// key is chosen by a consistent-hash ring, so adding/removing a node only
// remaps a fraction of keys. Each node has its own connection + mutex
// (hiredis contexts are not thread-safe), which is sufficient at this scale.
class RedisCache {
 public:
  struct Routing {
    std::string node_name;
    int         node_index = -1;
    uint32_t    vnode_hash = 0;
  };

  RedisCache(const std::vector<RedisNode>& nodes, int vnodes_per_node);

  // Cache ops. Keys are arbitrary (binary-safe) strings.
  std::optional<std::string> get(const std::string& key);
  void set(const std::string& key, const std::string& value, int ttl_sec);
  void del(const std::string& key);
  bool exists(const std::string& key);

  // Which node owns `key` (no I/O) — backs /cache/debug and routing logs.
  Routing route(const std::string& key) const;

  void flushAll();  // used when global ranking changes invalidate everything

  size_t nodeCount() const { return nodes_.size(); }
  size_t ringSize() const { return ring_.ringSize(); }

 private:
  struct NodeConn {
    RedisNode info;
    redisContext* ctx = nullptr;
    std::mutex mu;
    explicit NodeConn(RedisNode i) : info(std::move(i)) {}
  };

  NodeConn& nodeForKey(const std::string& key);
  // Runs `fn` with a healthy context for this node, reconnecting once on
  // failure. Returns false if the node is unreachable.
  bool ensureConnected(NodeConn& n);

  std::vector<std::unique_ptr<NodeConn>> nodes_;
  ConsistentHashRing ring_;
};

}  // namespace ta
