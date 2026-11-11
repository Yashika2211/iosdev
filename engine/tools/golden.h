// A fixed scripted performance and the hash of its rendered output. Every
// build (native arm64, native x86-64, wasm in each browser) must reproduce
// kGoldenHash48k exactly; if it doesn't, clients would hear different mixes.
// Change the constant only on purpose, when the sound itself changes.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "core/rng.h"
#include "jam/engine.h"

namespace jam::golden {

inline constexpr uint64_t kGoldenHash48k = 0xc000c45f02ab8e57ULL;
inline constexpr uint32_t kSeconds = 12;

inline Event makeEvent(int64_t tick, uint32_t seq, EventType type, uint8_t player, uint8_t a = 0, uint8_t b = 0,
                       uint16_t c = 0) {
  Event e;
  e.tick = tick;
  e.seq = seq;
  e.type = type;
  e.player = player;
  e.a = a;
  e.b = b;
  e.c = c;
  return e;
}

// Uses every instrument and every kind of event, including a tempo change.
inline std::vector<Event> script() {
  std::vector<Event> out;
  uint32_t seq = 0;
  Rng rng(42);
  for (int p = 0; p < kMaxPlayers; ++p)
    out.push_back(makeEvent(0, seq++, EventType::SetInstrument, static_cast<uint8_t>(p), static_cast<uint8_t>(p % 4)));
  for (int s = 0; s < 16; s += 4) out.push_back(makeEvent(0, seq++, EventType::SeqStep, 2, static_cast<uint8_t>(s), 120, 0));
  for (int s = 2; s < 16; s += 4) out.push_back(makeEvent(0, seq++, EventType::SeqStep, 2, static_cast<uint8_t>(s), 90, 2));
  for (int64_t tick = 0; tick < 960 * 16; tick += 120) {
    const auto player = static_cast<uint8_t>(rng.next() % kMaxPlayers);
    const auto note = static_cast<uint8_t>(36 + rng.next() % 48);
    out.push_back(makeEvent(tick, seq++, EventType::NoteOn, player, note, static_cast<uint8_t>(60 + rng.next() % 67)));
    out.push_back(makeEvent(tick + 200, seq++, EventType::NoteOff, player, note));
    if (tick % 480 == 0)
      out.push_back(makeEvent(tick, seq++, EventType::SetParam, player, 0, 0, static_cast<uint16_t>(rng.next())));
  }
  out.push_back(makeEvent(960 * 8, seq++, EventType::Tempo, 0, 0, 0, 1400));
  return out;
}

inline uint64_t fnv(uint64_t h, float x) {
  uint32_t bits;
  std::memcpy(&bits, &x, 4);
  for (int b = 0; b < 4; ++b) {
    h ^= (bits >> (b * 8)) & 0xFF;
    h *= 1099511628211ULL;
  }
  return h;
}

// Hashes frame by frame (L then R), so the result can't depend on block size.
inline uint64_t renderHash(uint32_t sampleRate, uint32_t blockSize, uint32_t seconds = kSeconds) {
  auto engine = std::make_unique<Engine>();
  engine->prepare(sampleRate);
  for (const auto& e : script()) engine->post(e);
  std::vector<float> l(blockSize), r(blockSize);
  uint64_t h = 14695981039346656037ULL;
  uint32_t remaining = sampleRate * seconds;
  while (remaining > 0) {
    const uint32_t n = std::min(blockSize, remaining);
    engine->render(l.data(), r.data(), n);
    for (uint32_t i = 0; i < n; ++i) {
      h = fnv(h, l[i]);
      h = fnv(h, r[i]);
    }
    remaining -= n;
  }
  return h;
}

}  // namespace jam::golden
