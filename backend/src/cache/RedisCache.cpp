#include "cache/RedisCache.h"

#include <cstdio>

namespace ta {

RedisCache::RedisCache(const std::vector<RedisNode>& nodes, int vnodes_per_node) {
  for (size_t i = 0; i < nodes.size(); ++i) {
    nodes_.push_back(std::make_unique<NodeConn>(nodes[i]));
    ring_.addNode(static_cast<int>(i), nodes[i].name, vnodes_per_node);
    ensureConnected(*nodes_.back());
  }
}

bool RedisCache::ensureConnected(NodeConn& n) {
  if (n.ctx != nullptr && n.ctx->err == 0) return true;
  if (n.ctx != nullptr) {
    redisFree(n.ctx);
    n.ctx = nullptr;
  }
  struct timeval tv {1, 0};  // 1s connect timeout
  n.ctx = redisConnectWithTimeout(n.info.host.c_str(), n.info.port, tv);
  if (n.ctx == nullptr || n.ctx->err != 0) {
    std::fprintf(stderr, "[cache] cannot connect to %s (%s:%d): %s\n",
                 n.info.name.c_str(), n.info.host.c_str(), n.info.port,
                 n.ctx ? n.ctx->errstr : "alloc failed");
    if (n.ctx) {
      redisFree(n.ctx);
      n.ctx = nullptr;
    }
    return false;
  }
  return true;
}

RedisCache::Routing RedisCache::route(const std::string& key) const {
  Routing r;
  uint32_t vh = 0;
  int idx = ring_.nodeFor(key, &vh);
  r.node_index = idx;
  r.vnode_hash = vh;
  if (idx >= 0) r.node_name = nodes_[idx]->info.name;
  return r;
}

RedisCache::NodeConn& RedisCache::nodeForKey(const std::string& key) {
  int idx = ring_.nodeFor(key);
  return *nodes_[idx];
}

std::optional<std::string> RedisCache::get(const std::string& key) {
  NodeConn& n = nodeForKey(key);
  std::lock_guard<std::mutex> lk(n.mu);
  if (!ensureConnected(n)) return std::nullopt;
  auto* reply = static_cast<redisReply*>(
      redisCommand(n.ctx, "GET %b", key.data(), key.size()));
  if (reply == nullptr) {
    ensureConnected(n);  // mark for reconnect next time
    return std::nullopt;
  }
  std::optional<std::string> out;
  if (reply->type == REDIS_REPLY_STRING) out = std::string(reply->str, reply->len);
  freeReplyObject(reply);
  return out;
}

void RedisCache::set(const std::string& key, const std::string& value,
                     int ttl_sec) {
  NodeConn& n = nodeForKey(key);
  std::lock_guard<std::mutex> lk(n.mu);
  if (!ensureConnected(n)) return;
  auto* reply = static_cast<redisReply*>(
      redisCommand(n.ctx, "SET %b %b EX %d", key.data(), key.size(),
                   value.data(), value.size(), ttl_sec));
  if (reply == nullptr) {
    ensureConnected(n);
    return;
  }
  freeReplyObject(reply);
}

void RedisCache::del(const std::string& key) {
  NodeConn& n = nodeForKey(key);
  std::lock_guard<std::mutex> lk(n.mu);
  if (!ensureConnected(n)) return;
  auto* reply = static_cast<redisReply*>(
      redisCommand(n.ctx, "DEL %b", key.data(), key.size()));
  if (reply) freeReplyObject(reply);
}

bool RedisCache::exists(const std::string& key) {
  NodeConn& n = nodeForKey(key);
  std::lock_guard<std::mutex> lk(n.mu);
  if (!ensureConnected(n)) return false;
  auto* reply = static_cast<redisReply*>(
      redisCommand(n.ctx, "EXISTS %b", key.data(), key.size()));
  if (reply == nullptr) {
    ensureConnected(n);
    return false;
  }
  bool present = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
  freeReplyObject(reply);
  return present;
}

void RedisCache::flushAll() {
  for (auto& n : nodes_) {
    std::lock_guard<std::mutex> lk(n->mu);
    if (!ensureConnected(*n)) continue;
    auto* reply = static_cast<redisReply*>(redisCommand(n->ctx, "FLUSHDB"));
    if (reply) freeReplyObject(reply);
  }
}

}  // namespace ta
