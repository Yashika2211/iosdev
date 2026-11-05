// Topology-preserving state-variable filter (Simper/Cytomic form). It stays
// stable when the cutoff moves quickly, which the XY pad and tilt control do.
#pragma once
#include "core/detmath.h"

namespace jam {

class Svf {
 public:
  struct Output {
    float low, band, high;
  };

  void setCoefficients(float hz, float resonance01, float sampleRate) {
    float norm = hz / sampleRate;
    if (norm > 0.45f) norm = 0.45f;
    if (norm < 0.0005f) norm = 0.0005f;
    const float g = dm::tanPi(norm);
    k_ = 2.0f - 1.9f * resonance01;
    a1_ = 1.0f / (1.0f + g * (g + k_));
    a2_ = g * a1_;
    a3_ = g * a2_;
  }

  Output process(float in) {
    const float v3 = in - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = dm::flush(2.0f * v1 - ic1_);
    ic2_ = dm::flush(2.0f * v2 - ic2_);
    return {v2, v1, in - k_ * v1 - v2};
  }

  void reset() { ic1_ = ic2_ = 0.0f; }

 private:
  float k_ = 2.0f, a1_ = 1.0f, a2_ = 0.0f, a3_ = 0.0f;
  float ic1_ = 0.0f, ic2_ = 0.0f;
};

}  // namespace jam
