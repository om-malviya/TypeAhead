#pragma once
#include <cmath>
#include "Config.h"
#include "Types.h"

namespace ta {

// Two modes: "count" returns total_count directly; "recency" blends
// all-time popularity with time-decayed recent activity so short-lived
// spikes don't permanently over-rank a query.
class Scorer {
 public:
  explicit Scorer(const Config& cfg)
      : cfg_(cfg), lambda_(std::log(2.0) / cfg.decay_half_life_sec) {}

  double decayedRecent(const Entry& e, int64_t now_ms) const {
    if (e.last_searched_ms == 0) return 0.0;
    double dt = (now_ms - e.last_searched_ms) / 1000.0;
    if (dt < 0) dt = 0;
    return e.recent_count * std::exp(-lambda_ * dt);
  }

  double score(const Entry& e, int64_t now_ms, bool recency) const {
    if (!recency) return static_cast<double>(e.total_count);
    return cfg_.w_total * std::log(1.0 + static_cast<double>(e.total_count)) +
           cfg_.w_recent * decayedRecent(e, now_ms);
  }

  double lambda() const { return lambda_; }

 private:
  const Config& cfg_;
  double lambda_;
};

}  // namespace ta
