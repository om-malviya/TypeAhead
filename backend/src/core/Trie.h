#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Types.h"

namespace ta {

// Prefix index. collect() returns all entries under a prefix; top-k ranking
// is left to the caller so the trie stays a pure lookup structure.
class Trie {
 public:
  void insert(const std::string& key, Entry* e);
  std::vector<Entry*> collect(const std::string& prefix) const;

  size_t wordCount() const { return word_count_; }

 private:
  struct Node {
    std::unordered_map<char, std::unique_ptr<Node>> children;
    Entry* entry = nullptr; // non-null only at a word boundary
  };

  const Node* descend(const std::string& prefix) const;
  static void gather(const Node* n, std::vector<Entry*>& out);

  Node root_;
  size_t word_count_ = 0;
};

}  // namespace ta
