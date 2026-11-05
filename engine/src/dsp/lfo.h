#pragma once
#include "core/detmath.h"

namespace jam {

class Lfo {
 public:
  // `updateRate` is how many times per second tick() is called.
  void setRate(float hz, float updateRate) { inc_ = hz / updateRate; }
  void reset(float phase = 0.0f) { phase_ = phase; }
  float tick() {
    const float v = dm::sinTurns(phase_);
    phase_ = dm::wrap01(phase_ + inc_);
    return v;
  }

 private:
  float phase_ = 0.0f, inc_ = 0.0f;
};

}  // namespace jam
