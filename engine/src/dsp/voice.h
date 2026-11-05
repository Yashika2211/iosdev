// One note of a synth: two oscillators, a sub, a filter and two envelopes.
#pragma once
#include <cstdint>

#include "core/detmath.h"
#include "dsp/adsr.h"
#include "dsp/lfo.h"
#include "dsp/oscillator.h"
#include "dsp/svf.h"

namespace jam {

struct Patch {
  Waveform wave1 = Waveform::Saw, wave2 = Waveform::Saw;
  float detuneSemis = 0.1f;
  float mix2 = 0.5f;
  float subLevel = 0.0f;
  Adsr::Params amp{};
  Adsr::Params filter{};
  float filterEnvOctaves = 2.0f;
  float lfoHz = 0.0f;
  float lfoOctaves = 0.0f;
  float gain = 0.3f;
  int polyphony = 8;
  float delaySend = 0.0f;
  float reverbSend = 0.0f;
};

class Voice {
 public:
  static constexpr int kControlInterval = 32;

  void prepare(const WavetableBank* bank, float sampleRate) {
    sampleRate_ = sampleRate;
    osc1_.setBank(bank);
    osc2_.setBank(bank);
    sub_.setWaveform(Waveform::Sine);
    active_ = false;
  }

  void noteOn(const Patch& patch, uint8_t note, uint8_t velocity, uint64_t order) {
    patch_ = &patch;
    note_ = note;
    order_ = order;
    velocity_ = static_cast<float>(velocity) / 127.0f;
    gate_ = true;
    active_ = true;

    const float hz = dm::midiToHz(static_cast<float>(note));
    baseHz_ = hz;
    osc1_.setWaveform(patch.wave1);
    osc2_.setWaveform(patch.wave2);
    osc1_.setFrequency(hz, sampleRate_);
    osc2_.setFrequency(dm::midiToHz(static_cast<float>(note) + patch.detuneSemis), sampleRate_);
    sub_.setFrequency(hz * 0.5f, sampleRate_);
    osc1_.reset(0.0f);
    osc2_.reset(0.37f);
    sub_.reset(0.0f);
    amp_.configure(patch.amp, sampleRate_);
    filt_.configure(patch.filter, sampleRate_);
    amp_.reset();
    filt_.reset();
    amp_.noteOn();
    filt_.noteOn();
    filter_.reset();
    lfo_.setRate(patch.lfoHz, sampleRate_ / kControlInterval);
    lfo_.reset(0.0f);
  }

  void noteOff() {
    gate_ = false;
    amp_.noteOff();
    filt_.noteOff();
  }

  // Runs on the fixed control grid, so results don't depend on block size.
  void control(float cutoffHz, float resonance) {
    const float mod = filt_.value() * patch_->filterEnvOctaves + lfo_.tick() * patch_->lfoOctaves;
    // Half-octave-per-octave key tracking around middle C.
    const float track = dm::pow2((static_cast<float>(note_) - 60.0f) / 24.0f);
    filter_.setCoefficients(cutoffHz * track * dm::pow2(mod), resonance, sampleRate_);
  }

  float next() {
    const float a = osc1_.next();
    const float b = osc2_.next();
    const float s = patch_->subLevel > 0.0f ? sub_.next() * patch_->subLevel : 0.0f;
    const float raw = a * (1.0f - patch_->mix2) + b * patch_->mix2 + s;
    const float out = filter_.process(raw).low;
    const float env = amp_.next();
    filt_.next();
    if (!amp_.active()) active_ = false;
    return out * env * velocity_ * patch_->gain;
  }

  bool active() const { return active_; }
  bool gate() const { return gate_; }
  uint8_t note() const { return note_; }
  uint64_t order() const { return order_; }
  void kill() { active_ = false; }

 private:
  const Patch* patch_ = nullptr;
  Oscillator osc1_, osc2_, sub_;
  Svf filter_;
  Adsr amp_, filt_;
  Lfo lfo_;
  float sampleRate_ = 48000.0f, velocity_ = 0.0f, baseHz_ = 440.0f;
  uint64_t order_ = 0;
  uint8_t note_ = 0;
  bool gate_ = false, active_ = false;
};

}  // namespace jam
