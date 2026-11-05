// Band-limited oscillators. Saw and square use polyBLEP; the wavetable uses a
// mipmap of tables with fewer harmonics at higher pitches.
#pragma once
#include <cstdint>
#include <memory>

#include "core/detmath.h"

namespace jam {

enum class Waveform : uint8_t { Sine, Saw, Square, Wavetable };

class WavetableBank {
 public:
  static constexpr int kSize = 2048;
  static constexpr int kLevels = 10;

  // Allocates and fills the tables. Call once, away from the audio thread.
  void build() {
    data_ = std::make_unique<float[]>(static_cast<size_t>(kLevels) * (kSize + 1));
    for (int level = 0; level < kLevels; ++level) {
      float* t = table(level);
      const int harmonics = 512 >> level;
      float peak = 0.0f;
      for (int i = 0; i < kSize; ++i) {
        const float p = static_cast<float>(i) / kSize;
        float v = 0.0f;
        for (int h = 1; h <= harmonics; ++h) {
          // Odd harmonics a little stronger, for a hollow, organ-like tone.
          const float amp = ((h & 1) ? 1.0f : 0.55f) / static_cast<float>(h);
          v += amp * dm::sinTurns(p * static_cast<float>(h));
        }
        t[i] = v;
        const float a = v < 0.0f ? -v : v;
        if (a > peak) peak = a;
      }
      for (int i = 0; i < kSize; ++i) t[i] /= peak;
      t[kSize] = t[0];
    }
  }

  const float* table(int level) const { return data_.get() + static_cast<size_t>(level) * (kSize + 1); }
  float* table(int level) { return data_.get() + static_cast<size_t>(level) * (kSize + 1); }

  // Highest-resolution level whose top harmonic stays below Nyquist.
  static int levelFor(float phaseInc) {
    int level = 0;
    while (level < kLevels - 1 && static_cast<float>(512 >> level) * phaseInc >= 0.5f) ++level;
    return level;
  }

 private:
  std::unique_ptr<float[]> data_;
};

class Oscillator {
 public:
  void setWaveform(Waveform w) { wave_ = w; }
  void setBank(const WavetableBank* bank) { bank_ = bank; }
  void reset(float phase = 0.0f) { phase_ = phase; }

  void setFrequency(float hz, float sampleRate) {
    inc_ = hz / sampleRate;
    if (inc_ > 0.49f) inc_ = 0.49f;
    level_ = WavetableBank::levelFor(inc_);
  }

  float next() {
    float out = 0.0f;
    switch (wave_) {
      case Waveform::Sine:
        out = dm::sinTurns(phase_);
        break;
      case Waveform::Saw:
        out = 2.0f * phase_ - 1.0f - blep(phase_);
        break;
      case Waveform::Square: {
        out = (phase_ < 0.5f ? 1.0f : -1.0f) + blep(phase_);
        float shifted = phase_ + 0.5f;
        if (shifted >= 1.0f) shifted -= 1.0f;
        out -= blep(shifted);
        break;
      }
      case Waveform::Wavetable: {
        const float* t = bank_->table(level_);
        const float pos = phase_ * WavetableBank::kSize;
        const int i = static_cast<int>(pos);
        const float frac = pos - static_cast<float>(i);
        out = t[i] + (t[i + 1] - t[i]) * frac;
        break;
      }
    }
    phase_ += inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return out;
  }

 private:
  float blep(float t) const {
    if (t < inc_) {
      const float x = t / inc_;
      return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - inc_) {
      const float x = (t - 1.0f) / inc_;
      return x * x + x + x + 1.0f;
    }
    return 0.0f;
  }

  const WavetableBank* bank_ = nullptr;
  Waveform wave_ = Waveform::Saw;
  float phase_ = 0.0f;
  float inc_ = 0.0f;
  int level_ = 0;
};

}  // namespace jam
