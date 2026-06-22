#pragma once
#include <atomic>
#include <vector>
#include <mutex>
#include <algorithm>

namespace ta {

// Atomic counters and a capped latency reservoir for /stats.
class Metrics {
 public:
  std::atomic<long long> cache_hits{0};
  std::atomic<long long> cache_misses{0};
  std::atomic<long long> db_reads{0};
  std::atomic<long long> db_write_statements{0}; // SQL statements issued, not rows
  std::atomic<long long> searches_received{0};
  std::atomic<long long> flush_ops{0};
  std::atomic<long long> rows_written{0};
  std::atomic<long long> suggest_requests{0};

  void recordSuggestLatency(double ms) {
    std::lock_guard<std::mutex> lk(mu_);
    if (lat_.size() < kCap) {
      lat_.push_back(ms);
    } else {
      lat_[ring_++ % kCap] = ms; // ring buffer, overwrites oldest
    }
  }

  double percentile(double p) {
    std::vector<double> copy;
    {
      std::lock_guard<std::mutex> lk(mu_);
      copy = lat_;
    }
    if (copy.empty()) return 0.0;
    size_t k = static_cast<size_t>((p / 100.0) * (copy.size() - 1));
    std::nth_element(copy.begin(), copy.begin() + k, copy.end());
    return copy[k];
  }

  double average() {
    std::lock_guard<std::mutex> lk(mu_);
    if (lat_.empty()) return 0.0;
    double s = 0;
    for (double v : lat_) s += v;
    return s / lat_.size();
  }

  double cacheHitRate() {
    long long h = cache_hits.load(), m = cache_misses.load();
    return (h + m) ? (double)h / (double)(h + m) : 0.0;
  }

 private:
  static constexpr size_t kCap = 200000;
  std::mutex mu_;
  std::vector<double> lat_;
  size_t ring_ = 0;
};

}  // namespace ta
