// Drums made entirely from oscillators and noise; there are no samples.
#pragma once
#include <cstdint>

#include "core/detmath.h"
#include "core/rng.h"
#include "dsp/svf.h"
#include "jam/events.h"

namespace jam {

class Kick {
 public:
  void prepare(float sr) {
    sr_ = sr;
    ampCoef_ = dm::decayCoef(0.16f, sr);
    pitchCoef_ = dm::decayCoef(0.03f, sr);
    clickCoef_ = dm::decayCoef(0.002f, sr);
  }
  void trigger(float vel) {
    amp_ = vel;
    pitch_ = 1.0f;
    click_ = vel;
    phase_ = 0.0f;
  }
  float next() {
    if (amp_ < 1e-4f) return 0.0f;
    const float hz = 45.0f + 170.0f * pitch_;
    phase_ = dm::wrap01(phase_ + hz / sr_);
    const float out = dm::sinTurns(phase_) * amp_ + click_ * 0.3f;
    amp_ *= ampCoef_;
    pitch_ *= pitchCoef_;
    click_ *= clickCoef_;
    return out;
  }
  bool active() const { return amp_ >= 1e-4f; }

 private:
  float sr_ = 48000.0f, amp_ = 0.0f, pitch_ = 0.0f, click_ = 0.0f, phase_ = 0.0f;
  float ampCoef_ = 0.0f, pitchCoef_ = 0.0f, clickCoef_ = 0.0f;
};

class Snare {
 public:
  void prepare(float sr) {
    sr_ = sr;
    toneCoef_ = dm::decayCoef(0.07f, sr);
    noiseCoef_ = dm::decayCoef(0.11f, sr);
    hp_.setCoefficients(1800.0f, 0.1f, sr);
  }
  void trigger(float vel) {
    tone_ = vel;
    noise_ = vel;
    p1_ = p2_ = 0.0f;
  }
  float next(Rng& rng) {
    if (noise_ < 1e-4f) return 0.0f;
    p1_ = dm::wrap01(p1_ + 185.0f / sr_);
    p2_ = dm::wrap01(p2_ + 330.0f / sr_);
    const float body = (dm::sinTurns(p1_) + 0.6f * dm::sinTurns(p2_)) * 0.45f * tone_;
    const float snap = hp_.process(rng.bipolar()).high * noise_ * 0.7f;
    tone_ *= toneCoef_;
    noise_ *= noiseCoef_;
    return body + snap;
  }
  bool active() const { return noise_ >= 1e-4f; }

 private:
  float sr_ = 48000.0f, tone_ = 0.0f, noise_ = 0.0f, p1_ = 0.0f, p2_ = 0.0f;
  float toneCoef_ = 0.0f, noiseCoef_ = 0.0f;
  Svf hp_;
};

class Hat {
 public:
  void prepare(float sr) {
    sr_ = sr;
    closedCoef_ = dm::decayCoef(0.022f, sr);
    openCoef_ = dm::decayCoef(0.16f, sr);
    hp_.setCoefficients(7000.0f, 0.2f, sr);
  }
  // A closed hat chokes a ringing open hat, as on a real kit.
  void trigger(float vel, bool open) {
    amp_ = vel;
    coef_ = open ? openCoef_ : closedCoef_;
  }
  float next(Rng& rng) {
    if (amp_ < 1e-4f) return 0.0f;
    // Six inharmonic squares give the metallic ring of a cymbal.
    static constexpr float kRatios[6] = {205.3f, 369.6f, 304.4f, 522.7f, 540.0f, 800.0f};
    float metal = 0.0f;
    for (int i = 0; i < 6; ++i) {
      phase_[i] = dm::wrap01(phase_[i] + kRatios[i] * 2.0f / sr_);
      metal += phase_[i] < 0.5f ? 1.0f : -1.0f;
    }
    const float out = hp_.process(metal * 0.12f + rng.bipolar() * 0.5f).high * amp_ * 0.5f;
    amp_ *= coef_;
    return out;
  }
  bool active() const { return amp_ >= 1e-4f; }

 private:
  float sr_ = 48000.0f, amp_ = 0.0f, coef_ = 0.0f, closedCoef_ = 0.0f, openCoef_ = 0.0f;
  float phase_[6] = {};
  Svf hp_;
};

class DrumKit {
 public:
  void prepare(float sr) {
    kick_.prepare(sr);
    snare_.prepare(sr);
    hat_.prepare(sr);
  }
  void trigger(uint8_t note, uint8_t velocity) {
    const float v = static_cast<float>(velocity) / 127.0f;
    switch (static_cast<DrumNote>(note % 4)) {
      case DrumNote::Kick: kick_.trigger(v); break;
      case DrumNote::Snare: snare_.trigger(v); break;
      case DrumNote::ClosedHat: hat_.trigger(v, false); break;
      case DrumNote::OpenHat: hat_.trigger(v, true); break;
    }
  }
  float next() { return kick_.next() * 0.9f + snare_.next(rng_) * 0.7f + hat_.next(rng_) * 0.5f; }
  int activeCount() const {
    return static_cast<int>(kick_.active()) + static_cast<int>(snare_.active()) +
           static_cast<int>(hat_.active());
  }

 private:
  Kick kick_;
  Snare snare_;
  Hat hat_;
  Rng rng_;
};

}  // namespace jam
