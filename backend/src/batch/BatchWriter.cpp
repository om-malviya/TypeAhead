#include "batch/BatchWriter.h"

#include <chrono>

namespace ta {

BatchWriter::BatchWriter(int batch_size, int interval_ms, FlushFn fn)
    : batch_size_(batch_size), interval_ms_(interval_ms), flush_(std::move(fn)) {}

BatchWriter::~BatchWriter() { stop(); }

void BatchWriter::start() {
  std::lock_guard<std::mutex> lk(mu_);
  if (running_) return;
  running_ = true;
  thread_ = std::thread(&BatchWriter::run, this);
}

void BatchWriter::stop() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!running_) return;
    running_ = false;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void BatchWriter::push(const std::string& q) {
  bool full = false;
  {
    std::lock_guard<std::mutex> lk(mu_);
    buf_[q] += 1;
    full = static_cast<int>(buf_.size()) >= batch_size_;
  }
  if (full) cv_.notify_one();  // flush promptly when the batch fills
}

size_t BatchWriter::pending() {
  std::lock_guard<std::mutex> lk(mu_);
  return buf_.size();
}

void BatchWriter::run() {
  while (true) {
    Buffer snapshot;
    bool stopping = false;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait_for(lk, std::chrono::milliseconds(interval_ms_), [this] {
        return !running_ || static_cast<int>(buf_.size()) >= batch_size_;
      });
      stopping = !running_;
      snapshot.swap(buf_);  // take everything buffered so far
    }
    if (!snapshot.empty()) flush_(snapshot);
    if (stopping) break;  // final drain done above before exiting
  }
}

}  // namespace ta
