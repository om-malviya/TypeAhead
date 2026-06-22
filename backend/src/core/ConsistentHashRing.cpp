#include "core/ConsistentHashRing.h"

namespace ta {

uint32_t ConsistentHashRing::hash(const std::string& s) {
  uint32_t h = 2166136261u;  // FNV offset basis
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;  // FNV prime
  }
  // fmix32 finalizer: FNV-1a alone clusters virtual node positions because
  // adjacent vnode strings ("redis-0#1", "redis-0#2") hash too similarly.
  h ^= h >> 16;
  h *= 0x85ebca6bu;
  h ^= h >> 13;
  h *= 0xc2b2ae35u;
  h ^= h >> 16;
  return h;
}

void ConsistentHashRing::addNode(int nodeIndex, const std::string& nodeName,
                                 int vnodes) {
  for (int i = 0; i < vnodes; ++i) {
    uint32_t h = hash(nodeName + "#" + std::to_string(i));
    ring_[h] = nodeIndex;
  }
}

int ConsistentHashRing::nodeFor(const std::string& key,
                                uint32_t* vnodeHash) const {
  if (ring_.empty()) return -1;
  uint32_t h = hash(key);
  auto it = ring_.lower_bound(h);
  if (it == ring_.end()) it = ring_.begin();  // wrap around the ring
  if (vnodeHash) *vnodeHash = it->first;
  return it->second;
}

}  // namespace ta
