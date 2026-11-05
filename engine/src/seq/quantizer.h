// Snaps notes to the shared grid. Pure integer math, shared by every client.
#pragma once
#include <cstdint>

namespace jam {

// First grid line at or after `tick`.
constexpr int64_t quantizeUp(int64_t tick, int64_t grid) {
  const int64_t rem = tick % grid;
  if (rem == 0) return tick;
  return rem > 0 ? tick + (grid - rem) : tick - rem;
}

// First grid line that is at least `lookahead` ticks in the future, so a note
// has time to reach every client before it sounds.
constexpr int64_t scheduleTick(int64_t nowTick, int64_t lookahead, int64_t grid) {
  return quantizeUp(nowTick + lookahead, grid);
}

}  // namespace jam
