#pragma once
#include "core/detmath.h"

namespace jam {

class Adsr {
 public:
  struct Params {
    float attack = 0.005f, decay = 0.1f, sustain = 0.7f, release = 0.2f;  // seconds, level
  };

  void configure(const Params& p, float sampleRate) {
    attackStep_ = 1.0f / (p.attack * sampleRate);
    decayCoef_ = dm::decayCoef(p.decay, sampleRate);
    releaseCoef_ = dm::decayCoef(p.release, sampleRate);
    sustain_ = p.sustain;
  }

  void noteOn() { stage_ = Stage::Attack; }
  void noteOff() {
    if (stage_ != Stage::Idle) stage_ = Stage::Release;
  }
  void reset() {
    stage_ = Stage::Idle;
    value_ = 0.0f;
  }

  bool active() const { return stage_ != Stage::Idle; }
  bool releasing() const { return stage_ == Stage::Release; }
  float value() const { return value_; }

  float next() {
    switch (stage_) {
      case Stage::Idle:
        break;
      case Stage::Attack:
        value_ += attackStep_;
        if (value_ >= 1.0f) {
          value_ = 1.0f;
          stage_ = Stage::Decay;
        }
        break;
      case Stage::Decay:
        value_ = sustain_ + (value_ - sustain_) * decayCoef_;
        break;
      case Stage::Release:
        value_ *= releaseCoef_;
        if (value_ < 1e-4f) reset();
        break;
    }
    return value_;
  }

 private:
  enum class Stage { Idle, Attack, Decay, Release };
  Stage stage_ = Stage::Idle;
  float value_ = 0.0f;
  float attackStep_ = 0.0f, decayCoef_ = 0.0f, releaseCoef_ = 0.0f, sustain_ = 0.0f;
};

}  // namespace jam
