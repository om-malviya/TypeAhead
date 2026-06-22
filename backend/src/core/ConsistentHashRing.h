#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace ta {

// Consistent hash ring. Each physical node gets many virtual positions so
// keys stay roughly balanced even with a small number of nodes.
class ConsistentHashRing {
 public:
  void addNode(int nodeIndex, const std::string& nodeName, int vnodes);
  int  nodeFor(const std::string& key, uint32_t* vnodeHash = nullptr) const;

  size_t ringSize() const { return ring_.size(); }
  bool empty() const { return ring_.empty(); }

  static uint32_t hash(const std::string& s); // FNV-1a + fmix32 finalizer

 private:
  std::map<uint32_t, int> ring_;  // ring position -> physical node index
};

}  // namespace ta
