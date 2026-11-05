// Measures CPU per voice as the extra time N sustained voices add over an idle
// engine, expressed as a percentage of one real-time core. Prints JSON too, so
// results can be stored in measurements/ exactly as measured.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

#include "jam/engine.h"

using namespace jam;

namespace {
constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 128;
constexpr uint32_t kSeconds = 20;
constexpr int kRepeats = 5;

double renderSeconds(Instrument instrument, int voices) {
  auto engine = std::make_unique<Engine>();
  engine->prepare(kRate);
  std::vector<float> l(kBlock), r(kBlock);
  uint32_t seq = 0;
  int remaining = voices;
  for (int p = 0; p < kMaxPlayers && remaining > 0; ++p) {
    Event set;
    set.seq = seq++;
    set.type = EventType::SetInstrument;
    set.player = static_cast<uint8_t>(p);
    set.a = static_cast<uint8_t>(instrument);
    engine->post(set);
    const int here = std::min(remaining, 8);
    for (int v = 0; v < here; ++v) {
      Event on;
      on.seq = seq++;
      on.type = EventType::NoteOn;
      on.player = static_cast<uint8_t>(p);
      on.a = static_cast<uint8_t>(48 + v * 3);
      on.b = 100;
      engine->post(on);
    }
    remaining -= here;
  }
  // Skip the attack so voices are in a steady state.
  for (uint32_t i = 0; i < kRate / kBlock; ++i) engine->render(l.data(), r.data(), kBlock);
  const auto start = std::chrono::steady_clock::now();
  const uint32_t blocks = kRate * kSeconds / kBlock;
  for (uint32_t i = 0; i < blocks; ++i) engine->render(l.data(), r.data(), kBlock);
  const std::chrono::duration<double> d = std::chrono::steady_clock::now() - start;
  if (engine->stats().activeVoices < voices) std::fprintf(stderr, "warning: fewer active voices than requested\n");
  return d.count();
}

double best(Instrument instrument, int voices) {
  double t = 1e30;
  for (int i = 0; i < kRepeats; ++i) t = std::min(t, renderSeconds(instrument, voices));
  return t;
}
}  // namespace

int main() {
  const double idle = best(Instrument::Lead, 0);
  std::printf("{\n  \"sample_rate\": %u, \"block\": %u, \"seconds\": %u, \"repeats\": %d,\n", kRate, kBlock, kSeconds, kRepeats);
  std::printf("  \"idle_percent_of_core\": %.4f,\n  \"results\": [\n", idle / kSeconds * 100.0);
  const struct { const char* name; Instrument instrument; int voices; } cases[] = {
      {"lead", Instrument::Lead, 8},  {"lead", Instrument::Lead, 32},
      {"pads", Instrument::Pads, 8},  {"pads", Instrument::Pads, 32},
  };
  bool first = true;
  for (const auto& c : cases) {
    const double t = best(c.instrument, c.voices);
    const double perVoice = (t - idle) / c.voices;
    std::printf("%s    {\"instrument\": \"%s\", \"voices\": %d, \"total_percent_of_core\": %.4f, \"percent_per_voice\": %.5f, \"ns_per_voice_sample\": %.2f}",
                first ? "" : ",\n", c.name, c.voices, t / kSeconds * 100.0, perVoice / kSeconds * 100.0,
                perVoice * 1e9 / (kRate * kSeconds));
    first = false;
  }
  std::printf("\n  ]\n}\n");
}
