#pragma once
#include <string>
#include <cctype>
#include <chrono>
#include <cstdint>

namespace ta {

// Current wall-clock time in epoch milliseconds.
inline int64_t nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Lowercase + trim. Queries/prefixes are normalized so matching is
// case-insensitive and whitespace-insensitive at the edges.
inline std::string normalize(std::string s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace((unsigned char)s[b])) ++b;
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  std::string out;
  out.reserve(e - b);
  for (size_t i = b; i < e; ++i) out.push_back((char)std::tolower((unsigned char)s[i]));
  return out;
}

}  // namespace ta
