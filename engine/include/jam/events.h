// The deterministic event model. Given the same ordered stream of events, the
// same sample rate and the same start position, the engine produces
// bit-identical audio on every platform.
#pragma once
#include <cstdint>

namespace jam {

inline constexpr int kMaxPlayers = 8;
inline constexpr int64_t kTicksPerQuarter = 960;
inline constexpr int64_t kTicksPerStep = kTicksPerQuarter / 4;  // 16th note
inline constexpr int kStepsPerPattern = 16;

enum class Instrument : uint8_t { Lead = 0, Bass = 1, Drums = 2, Pads = 3 };
enum class DrumNote : uint8_t { Kick = 0, Snare = 1, ClosedHat = 2, OpenHat = 3 };
enum class Param : uint8_t { Cutoff = 0, Resonance = 1 };

enum class EventType : uint8_t {
  NoteOn = 0,         // a = note, b = velocity
  NoteOff = 1,        // a = note
  SetParam = 2,       // a = Param, c = value 0..65535
  SetInstrument = 3,  // a = Instrument
  SeqStep = 4,        // a = step, b = velocity (0 clears), c = note
  SeqClear = 5,
  Tempo = 6,          // c = beats per minute * 10
  AllNotesOff = 7,
};

// Integer fields only: floats would invite rounding differences on the wire.
struct Event {
  int64_t tick = 0;   // position on the shared grid
  uint32_t seq = 0;   // tie-break, so events on one tick have a total order
  EventType type = EventType::NoteOn;
  uint8_t player = 0;
  uint8_t a = 0;
  uint8_t b = 0;
  uint16_t c = 0;
};

constexpr bool eventBefore(const Event& x, const Event& y) {
  return x.tick != y.tick ? x.tick < y.tick : x.seq < y.seq;
}

}  // namespace jam
