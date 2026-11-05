// Stereo ping-pong delay. The buffer is allocated in prepare(), never in process().
#pragma once
#include <memory>

#include "core/detmath.h"

namespace jam {

class PingPongDelay {
 public:
  void prepare(float sr, float maxSeconds = 2.0f) {
    size_ = static_cast<int>(sr * maxSeconds) + 1;
    left_ = std::make_unique<float[]>(static_cast<size_t>(size_));
    right_ = std::make_unique<float[]>(static_cast<size_t>(size_));
    sr_ = sr;
    setTime(0.375f);
  }
  void setTime(float seconds) {
    int d = static_cast<int>(seconds * sr_);
    if (d < 1) d = 1;
    if (d >= size_) d = size_ - 1;
    delay_ = d;
  }
  void process(float in, float& outL, float& outR) {
    int r = pos_ - delay_;
    if (r < 0) r += size_;
    const float l = left_[r];
    const float rr = right_[r];
    damp_ = dm::flush(damp_ + (rr - damp_) * 0.45f);
    left_[pos_] = dm::flush(in + damp_ * feedback_);
    right_[pos_] = dm::flush(l * feedback_);
    if (++pos_ == size_) pos_ = 0;
    outL = l;
    outR = rr;
  }

 private:
  std::unique_ptr<float[]> left_, right_;
  float sr_ = 48000.0f, feedback_ = 0.42f, damp_ = 0.0f;
  int size_ = 0, delay_ = 1, pos_ = 0;
};

}  // namespace jam
