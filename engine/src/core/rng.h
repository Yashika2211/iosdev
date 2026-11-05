#pragma once
#include <cstdint>

namespace jam {

// xorshift32: integer-only, so noise is identical on every platform.
class Rng {
 public:
  explicit constexpr Rng(uint32_t seed = 0x9E3779B9u) : s_(seed ? seed : 1u) {}
  uint32_t next() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return s_;
  }
  // Uniform in [-1, 1).
  float bipolar() {
    return static_cast<float>(static_cast<int32_t>(next())) * (1.0f / 2147483648.0f);
  }

 private:
  uint32_t s_;
};

}  // namespace jam
