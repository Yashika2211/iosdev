// Stereo-linked feed-forward compressor. Working in linear gain with a fixed
// 4:1 ratio means sqrt is enough, so it needs no log or pow from libm.
#pragma once
#include <cmath>

#include "core/detmath.h"

namespace jam {

class Compressor {
 public:
  void prepare(float sr) {
    attack_ = dm::decayCoef(0.003f, sr);
    release_ = dm::decayCoef(0.12f, sr);
    threshold_ = dm::dbToGain(-12.0f);
    makeup_ = dm::dbToGain(3.0f);
  }
  void process(float& l, float& r) {
    const float al = l < 0.0f ? -l : l;
    const float ar = r < 0.0f ? -r : r;
    const float level = al > ar ? al : ar;
    const float coef = level > env_ ? attack_ : release_;
    env_ = dm::flush(level + (env_ - level) * coef);
    float gain = 1.0f;
    if (env_ > threshold_) {
      // (threshold / env)^(3/4) is the gain of a 4:1 ratio above threshold.
      const float x = threshold_ / env_;
      const float s = std::sqrt(x);
      gain = s * std::sqrt(s);
    }
    l *= gain * makeup_;
    r *= gain * makeup_;
    gainReduction_ = gain;
  }
  float gainReduction() const { return gainReduction_; }

 private:
  float attack_ = 0.0f, release_ = 0.0f, threshold_ = 1.0f, makeup_ = 1.0f;
  float env_ = 0.0f, gainReduction_ = 1.0f;
};

}  // namespace jam
