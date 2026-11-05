#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "core/detmath.h"
#include "core/rng.h"

using namespace jam;
using Catch::Approx;

TEST_CASE("sinTurns matches sin across several turns") {
  for (int i = -4000; i <= 4000; ++i) {
    const float t = static_cast<float>(i) / 1000.0f;
    REQUIRE(std::fabs(dm::sinTurns(t) - std::sin(static_cast<double>(t) * 6.283185307179586)) < 1e-4);
  }
}

TEST_CASE("pow2 matches exact powers and is continuous") {
  REQUIRE(dm::pow2(0.0f) == Approx(1.0f).epsilon(1e-5));
  REQUIRE(dm::pow2(10.0f) == Approx(1024.0f).epsilon(1e-5));
  REQUIRE(dm::pow2(-3.0f) == Approx(0.125f).epsilon(1e-5));
  for (int i = -2000; i <= 2000; ++i) {
    const float x = static_cast<float>(i) / 100.0f;
    REQUIRE(dm::pow2(x) == Approx(std::pow(2.0, static_cast<double>(x))).epsilon(1e-4));
  }
}

TEST_CASE("midiToHz gives concert pitch") {
  REQUIRE(dm::midiToHz(69.0f) == Approx(440.0f).epsilon(1e-4));
  REQUIRE(dm::midiToHz(60.0f) == Approx(261.626f).epsilon(1e-3));
}

TEST_CASE("Rng output is fixed and bounded") {
  Rng a(123), b(123);
  for (int i = 0; i < 10000; ++i) {
    const float x = a.bipolar();
    REQUIRE(x == b.bipolar());
    REQUIRE(x >= -1.0f);
    REQUIRE(x <= 1.0f);
  }
}
