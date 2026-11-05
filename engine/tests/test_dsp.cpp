#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "drums/drums.h"
#include "dsp/adsr.h"
#include "dsp/oscillator.h"
#include "dsp/svf.h"

using namespace jam;

namespace {
// Single-bin DFT magnitude, normalized so a full-scale sine reads 1.
double binMagnitude(const std::vector<float>& x, double hz, double sr) {
  double re = 0, im = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    // Hann window, to keep leakage from masking real aliases.
    const double w = 0.5 - 0.5 * std::cos(2 * M_PI * static_cast<double>(i) / static_cast<double>(x.size() - 1));
    const double ph = 2 * M_PI * hz * static_cast<double>(i) / sr;
    re += w * x[i] * std::cos(ph);
    im += w * x[i] * std::sin(ph);
  }
  return 4.0 * std::sqrt(re * re + im * im) / static_cast<double>(x.size());
}

double rms(const std::vector<float>& x) {
  double s = 0;
  for (float v : x) s += static_cast<double>(v) * v;
  return std::sqrt(s / static_cast<double>(x.size()));
}

std::vector<float> renderOsc(Waveform w, float hz, float sr, int n, const WavetableBank* bank = nullptr) {
  Oscillator o;
  o.setBank(bank);
  o.setWaveform(w);
  o.setFrequency(hz, sr);
  std::vector<float> out(static_cast<size_t>(n));
  for (auto& s : out) s = o.next();
  return out;
}
}  // namespace

TEST_CASE("polyBLEP saw has much less aliasing than a naive saw") {
  const float sr = 48000, hz = 3517;
  const int n = 48000;
  auto blep = renderOsc(Waveform::Saw, hz, sr, n);
  std::vector<float> naive(static_cast<size_t>(n));
  float ph = 0;
  for (auto& s : naive) {
    s = 2 * ph - 1;
    ph += hz / sr;
    if (ph >= 1) ph -= 1;
  }
  // Harmonic 7 at 24619 Hz folds back to 23381 Hz; 8 folds to 19864 Hz.
  const double aliasBlep = binMagnitude(blep, 19864, sr) + binMagnitude(blep, 23381, sr);
  const double aliasNaive = binMagnitude(naive, 19864, sr) + binMagnitude(naive, 23381, sr);
  INFO("blep=" << aliasBlep << " naive=" << aliasNaive);
  REQUIRE(aliasBlep < aliasNaive * 0.5);
}

TEST_CASE("sine and square produce the expected fundamentals") {
  const float sr = 48000;
  auto sine = renderOsc(Waveform::Sine, 1000, sr, 48000);
  REQUIRE(binMagnitude(sine, 1000, sr) == Catch::Approx(1.0).margin(0.02));
  REQUIRE(binMagnitude(sine, 2000, sr) < 0.001);
  auto square = renderOsc(Waveform::Square, 1000, sr, 48000);
  // An ideal square: 4/pi fundamental, 1/3 of that at the third harmonic, no second.
  REQUIRE(binMagnitude(square, 1000, sr) == Catch::Approx(4.0 / M_PI).margin(0.03));
  REQUIRE(binMagnitude(square, 3000, sr) == Catch::Approx(4.0 / (3 * M_PI)).margin(0.03));
  REQUIRE(binMagnitude(square, 2000, sr) < 0.01);
}

TEST_CASE("wavetable mipmap has no energy above Nyquist at high pitch") {
  WavetableBank bank;
  bank.build();
  const float sr = 48000, hz = 5000;
  auto x = renderOsc(Waveform::Wavetable, hz, sr, 48000, &bank);
  REQUIRE(rms(x) > 0.1);
  // Would-be aliases of harmonics 5 and 6 at 23000 and 18000 Hz.
  REQUIRE(binMagnitude(x, 23000, sr) < 0.01);
  REQUIRE(binMagnitude(x, 18000, sr) < 0.01);
}

TEST_CASE("ADSR moves through its stages and ends idle") {
  Adsr env;
  env.configure({0.01f, 0.05f, 0.5f, 0.05f}, 48000);
  env.noteOn();
  float peak = 0;
  for (int i = 0; i < 480; ++i) peak = std::max(peak, env.next());
  REQUIRE(peak == Catch::Approx(1.0f).margin(0.01));
  for (int i = 0; i < 48000; ++i) env.next();
  REQUIRE(env.value() == Catch::Approx(0.5f).margin(0.001));
  env.noteOff();
  for (int i = 0; i < 48000; ++i) env.next();
  REQUIRE_FALSE(env.active());
}

TEST_CASE("SVF lowpass passes low and rejects high frequencies") {
  const float sr = 48000;
  Svf f;
  f.setCoefficients(1000, 0.1f, sr);
  auto gainAt = [&](float hz) {
    f.reset();
    auto in = renderOsc(Waveform::Sine, hz, sr, 48000);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = f.process(in[i]).low;
    return rms(out) / rms(in);
  };
  REQUIRE(gainAt(100) == Catch::Approx(1.0).margin(0.05));
  REQUIRE(gainAt(10000) < 0.02);
}

TEST_CASE("SVF stays stable under a fast cutoff sweep at high resonance") {
  Svf f;
  Rng rng;
  for (int i = 0; i < 480000; ++i) {
    if (i % 32 == 0) f.setCoefficients(20.0f + 20000.0f * (rng.bipolar() * 0.5f + 0.5f), 1.0f, 48000);
    const float y = f.process(rng.bipolar()).low;
    REQUIRE(std::isfinite(y));
    REQUIRE(std::fabs(y) < 100.0f);
  }
}

TEST_CASE("each drum sounds and then falls silent") {
  for (uint8_t note = 0; note < 4; ++note) {
    DrumKit kit;
    kit.prepare(48000);
    kit.trigger(note, 127);
    std::vector<float> hit(4800);
    for (auto& s : hit) s = kit.next();
    INFO("drum " << static_cast<int>(note));
    REQUIRE(rms(hit) > 0.01);
    for (int i = 0; i < 48000 * 3; ++i) kit.next();
    REQUIRE(kit.activeCount() == 0);
  }
}
