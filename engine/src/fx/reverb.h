// Eight-line feedback delay network with a Householder mixing matrix: dense,
// energy-preserving before damping, and cheap enough to run on phones.
#pragma once
#include <array>
#include <memory>

#include "core/detmath.h"

namespace jam {

class FdnReverb {
 public:
  static constexpr int kLines = 8;

  void prepare(float sr) {
    static constexpr std::array<int, kLines> kBase = {1499, 1889, 2381, 2999, 3343, 3761, 4231, 4679};
    size_t total = 0;
    for (int i = 0; i < kLines; ++i) {
      len_[i] = static_cast<int>(static_cast<float>(kBase[i]) * sr / 48000.0f);
      total += static_cast<size_t>(len_[i]);
    }
    storage_ = std::make_unique<float[]>(total);
    float* p = storage_.get();
    for (int i = 0; i < kLines; ++i) {
      line_[i] = p;
      p += len_[i];
      pos_[i] = 0;
      lp_[i] = 0.0f;
    }
  }

  void process(float in, float& outL, float& outR) {
    std::array<float, kLines> v;
    float sum = 0.0f;
    for (int i = 0; i < kLines; ++i) {
      v[i] = line_[i][pos_[i]];
      sum += v[i];
    }
    // Householder: x - (2/N) * sum(x).
    const float h = sum * (2.0f / kLines);
    for (int i = 0; i < kLines; ++i) {
      // Damp after the matrix: the matrix is unitary and the filter never adds
      // gain, so with feedback below 1 the loop cannot grow.
      lp_[i] = dm::flush(lp_[i] + ((v[i] - h) - lp_[i]) * damping_);
      line_[i][pos_[i]] = dm::flush(lp_[i] * feedback_ + in);
      if (++pos_[i] == len_[i]) pos_[i] = 0;
    }
    outL = (v[0] - v[2] + v[4] - v[6]) * 0.35f;
    outR = (v[1] - v[3] + v[5] - v[7]) * 0.35f;
  }

 private:
  std::unique_ptr<float[]> storage_;
  std::array<float*, kLines> line_{};
  std::array<int, kLines> len_{}, pos_{};
  std::array<float, kLines> lp_{};
  float feedback_ = 0.84f, damping_ = 0.55f;
};

}  // namespace jam
