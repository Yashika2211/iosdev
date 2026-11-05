#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace jam {

// Wait-free single-producer single-consumer ring. It is how events get from a
// UI or network thread to the audio thread without locks.
template <typename T, size_t Capacity>
class SpscQueue {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

 public:
  bool push(const T& v) {
    const size_t w = write_.load(std::memory_order_relaxed);
    if (w - read_.load(std::memory_order_acquire) == Capacity) return false;
    items_[w & (Capacity - 1)] = v;
    write_.store(w + 1, std::memory_order_release);
    return true;
  }
  bool pop(T& out) {
    const size_t r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire)) return false;
    out = items_[r & (Capacity - 1)];
    read_.store(r + 1, std::memory_order_release);
    return true;
  }

 private:
  std::array<T, Capacity> items_{};
  alignas(64) std::atomic<size_t> write_{0};
  alignas(64) std::atomic<size_t> read_{0};
};

}  // namespace jam
