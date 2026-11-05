#include <catch2/catch_test_macros.hpp>

#include "jam/events.h"
#include "seq/quantizer.h"
#include "seq/sequencer.h"

using namespace jam;

TEST_CASE("quantizeUp snaps to the next grid line") {
  STATIC_REQUIRE(quantizeUp(0, 240) == 0);
  STATIC_REQUIRE(quantizeUp(1, 240) == 240);
  STATIC_REQUIRE(quantizeUp(239, 240) == 240);
  STATIC_REQUIRE(quantizeUp(240, 240) == 240);
  STATIC_REQUIRE(quantizeUp(-1, 240) == 0);
}

TEST_CASE("scheduleTick always leaves at least the lookahead") {
  for (int64_t now = 0; now < 2000; now += 7) {
    for (int64_t look : {0, 50, 240, 500}) {
      const int64_t t = scheduleTick(now, look, kTicksPerStep);
      REQUIRE(t >= now + look);
      REQUIRE(t < now + look + kTicksPerStep);
      REQUIRE(t % kTicksPerStep == 0);
    }
  }
}

TEST_CASE("sequencer wraps its pattern and ignores invalid steps") {
  StepSequencer s;
  s.set(3, 60, 100);
  s.set(99, 1, 1);
  s.set(-1, 1, 1);
  REQUIRE(s.at(3).note == 60);
  REQUIRE(s.at(3 + kStepsPerPattern).velocity == 100);
  REQUIRE(s.at(4).velocity == 0);
  s.clear();
  REQUIRE(s.at(3).velocity == 0);
}

TEST_CASE("events have a total order") {
  Event a, b;
  a.tick = 10; a.seq = 5;
  b.tick = 10; b.seq = 6;
  REQUIRE(eventBefore(a, b));
  REQUIRE_FALSE(eventBefore(b, a));
  b.tick = 9;
  REQUIRE(eventBefore(b, a));
}
