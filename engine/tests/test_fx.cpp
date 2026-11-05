#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "core/rng.h"
#include "fx/compressor.h"
#include "fx/delay.h"
#include "fx/reverb.h"

using namespace jam;

TEST_CASE("delay echoes after its delay time and decays") {
  PingPongDelay d;
  d.prepare(48000);
  d.setTime(0.1f);
  float l, r;
  d.process(1.0f, l, r);
  float firstEcho = 0;
  int firstAt = -1;
  for (int i = 1; i < 48000; ++i) {
    d.process(0.0f, l, r);
    if (firstAt < 0 && std::fabs(l) > 0.01f) {
      firstAt = i;
      firstEcho = l;
    }
  }
  REQUIRE(firstAt == 4800);
  REQUIRE(firstEcho == 1.0f);
  for (int i = 0; i < 48000 * 20; ++i) d.process(0.0f, l, r);
  REQUIRE(std::fabs(l) < 1e-4f);
  REQUIRE(std::fabs(r) < 1e-4f);
}

TEST_CASE("reverb tail is dense, then decays to silence") {
  FdnReverb v;
  v.prepare(48000);
  float l, r;
  v.process(1.0f, l, r);
  double earlyEnergy = 0;
  for (int i = 0; i < 48000; ++i) {
    v.process(0.0f, l, r);
    earlyEnergy += static_cast<double>(l) * l + static_cast<double>(r) * r;
  }
  REQUIRE(earlyEnergy > 0.01);
  double lateEnergy = 0;
  for (int i = 0; i < 48000 * 30; ++i) {
    v.process(0.0f, l, r);
    REQUIRE(std::isfinite(l));
    if (i > 48000 * 29) lateEnergy += static_cast<double>(l) * l + static_cast<double>(r) * r;
  }
  REQUIRE(lateEnergy < earlyEnergy * 1e-6);
}

TEST_CASE("reverb stays bounded under sustained noise") {
  FdnReverb v;
  v.prepare(48000);
  Rng rng;
  float l, r;
  for (int i = 0; i < 48000 * 30; ++i) {
    v.process(rng.bipolar(), l, r);
    REQUIRE(std::fabs(l) < 50.0f);
    REQUIRE(std::fabs(r) < 50.0f);
  }
}

TEST_CASE("compressor reduces loud signals and leaves quiet ones alone") {
  Compressor c;
  c.prepare(48000);
  float l = 0, r = 0;
  for (int i = 0; i < 48000; ++i) {
    l = r = 1.0f;
    c.process(l, r);
  }
  // Input at 0 dB is 12 dB over threshold; 4:1 keeps 3 dB of it, plus 3 dB makeup.
  const float expected = std::pow(10.0f, -6.0f / 20.0f);
  REQUIRE(std::fabs(l - expected) < 0.02f);

  Compressor quiet;
  quiet.prepare(48000);
  for (int i = 0; i < 48000; ++i) {
    l = r = 0.05f;
    quiet.process(l, r);
  }
  REQUIRE(quiet.gainReduction() == 1.0f);
}
