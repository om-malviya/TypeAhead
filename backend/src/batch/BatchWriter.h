#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace ta {

// Coalesces search submissions and flushes in bulk. Flushes on size or
// interval, whichever comes first. A crash before a flush loses at most
// one batch worth of counts (see docs/interview-notes.md).
class BatchWriter {
 public:
  using Buffer  = std::unordered_map<std::string, long long>;
  using FlushFn = std::function<void(Buffer&)>;

  BatchWriter(int batch_size, int interval_ms, FlushFn fn);
  ~BatchWriter();

  void start();
  void stop();   // signals, drains, joins
  void push(const std::string& q);
  size_t pending();

 private:
  void run();

  int     batch_size_;
  int     interval_ms_;
  FlushFn flush_;

  std::mutex              mu_;
  std::condition_variable cv_;
  Buffer                  buf_;
  bool                    running_ = false;
  std::thread             thread_;
};

}  // namespace ta
