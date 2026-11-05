#pragma once
#include <array>
#include <cstdint>

#include "jam/events.h"

namespace jam {

class StepSequencer {
 public:
  struct Step {
    uint16_t note = 0;
    uint8_t velocity = 0;  // zero means the step is off
  };

  void set(int step, uint16_t note, uint8_t velocity) {
    if (step >= 0 && step < kStepsPerPattern) steps_[static_cast<size_t>(step)] = {note, velocity};
  }
  void clear() { steps_ = {}; }

  // The step that plays at a given grid position.
  const Step& at(int64_t stepIndex) const {
    return steps_[static_cast<size_t>(stepIndex % kStepsPerPattern)];
  }

 private:
  std::array<Step, kStepsPerPattern> steps_{};
};

}  // namespace jam
