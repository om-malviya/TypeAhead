#include "core/Trie.h"

namespace ta {

void Trie::insert(const std::string& key, Entry* e) {
  Node* cur = &root_;
  for (char c : key) {
    auto& child = cur->children[c];
    if (!child) child = std::make_unique<Node>();
    cur = child.get();
  }
  if (cur->entry == nullptr) ++word_count_;
  cur->entry = e;
}

const Trie::Node* Trie::descend(const std::string& prefix) const {
  const Node* cur = &root_;
  for (char c : prefix) {
    auto it = cur->children.find(c);
    if (it == cur->children.end()) return nullptr;
    cur = it->second.get();
  }
  return cur;
}

void Trie::gather(const Node* n, std::vector<Entry*>& out) {
  if (n->entry) out.push_back(n->entry);
  for (const auto& [c, child] : n->children) gather(child.get(), out);
}

std::vector<Entry*> Trie::collect(const std::string& prefix) const {
  std::vector<Entry*> out;
  const Node* start = descend(prefix);
  if (start) gather(start, out);
  return out;
}

}  // namespace ta
