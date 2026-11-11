// A flat C API over jam::Engine for JavaScript. Pointers cross the boundary as
// plain numbers; 64-bit ticks are split into two 32-bit halves.
#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstdint>
#include <new>

#include "golden.h"
#include "jam/engine.h"

namespace {

constexpr uint32_t kMaxFrames = 1024;

struct Host {
  jam::Engine engine;
  float left[kMaxFrames];
  float right[kMaxFrames];
  float peak = 0.0f;
};

jam::Event unpack(int32_t tickLo, int32_t tickHi, uint32_t seq, uint32_t packed, uint32_t c) {
  jam::Event e;
  e.tick = static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(tickHi)) << 32) |
                                static_cast<uint32_t>(tickLo));
  e.seq = seq;
  e.type = static_cast<jam::EventType>(packed & 0xFF);
  e.player = static_cast<uint8_t>((packed >> 8) & 0xFF);
  e.a = static_cast<uint8_t>((packed >> 16) & 0xFF);
  e.b = static_cast<uint8_t>((packed >> 24) & 0xFF);
  e.c = static_cast<uint16_t>(c);
  return e;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE Host* jam_create(uint32_t sampleRate, uint32_t bpmTimes10) {
  auto* h = new (std::nothrow) Host();
  if (h) h->engine.prepare(sampleRate, bpmTimes10);
  return h;
}

EMSCRIPTEN_KEEPALIVE void jam_destroy(Host* h) { delete h; }

EMSCRIPTEN_KEEPALIVE int jam_post(Host* h, int32_t tickLo, int32_t tickHi, uint32_t seq, uint32_t packed, uint32_t c) {
  return h->engine.post(unpack(tickLo, tickHi, seq, packed, c)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void jam_seek(Host* h, int32_t tickLo, int32_t tickHi) {
  h->engine.seek(unpack(tickLo, tickHi, 0, 0, 0).tick);
}

EMSCRIPTEN_KEEPALIVE void jam_render(Host* h, uint32_t frames) {
  frames = std::min(frames, kMaxFrames);
  h->engine.render(h->left, h->right, frames);
  float peak = h->peak;
  for (uint32_t i = 0; i < frames; ++i) {
    const float a = h->left[i] < 0 ? -h->left[i] : h->left[i];
    const float b = h->right[i] < 0 ? -h->right[i] : h->right[i];
    peak = std::max(peak, std::max(a, b));
  }
  h->peak = peak;
}

EMSCRIPTEN_KEEPALIVE float* jam_left(Host* h) { return h->left; }
EMSCRIPTEN_KEEPALIVE float* jam_right(Host* h) { return h->right; }

// Peak since the last call, then reset.
EMSCRIPTEN_KEEPALIVE float jam_take_peak(Host* h) {
  const float p = h->peak;
  h->peak = 0.0f;
  return p;
}

EMSCRIPTEN_KEEPALIVE int jam_active_voices(Host* h) { return h->engine.stats().activeVoices; }
EMSCRIPTEN_KEEPALIVE uint32_t jam_late_events(Host* h) { return h->engine.stats().lateEvents; }
EMSCRIPTEN_KEEPALIVE uint32_t jam_dropped_events(Host* h) { return h->engine.stats().droppedEvents; }
EMSCRIPTEN_KEEPALIVE double jam_tick(Host* h) { return static_cast<double>(h->engine.stats().tick); }
EMSCRIPTEN_KEEPALIVE double jam_sample_position(Host* h) {
  return static_cast<double>(h->engine.stats().samplePosition);
}

// Renders the golden script; must equal golden::kGoldenHash48k at 48 kHz.
EMSCRIPTEN_KEEPALIVE uint64_t jam_golden_hash(uint32_t sampleRate, uint32_t blockSize) {
  return jam::golden::renderHash(sampleRate, blockSize);
}

// Benchmark helper: an engine with `voices` sustained notes of one instrument.
EMSCRIPTEN_KEEPALIVE Host* jam_bench_create(uint32_t instrument, uint32_t voices) {
  Host* h = jam_create(48000, 1200);
  if (!h) return nullptr;
  uint32_t seq = 0;
  uint32_t remaining = voices;
  for (uint8_t p = 0; p < jam::kMaxPlayers && remaining > 0; ++p) {
    h->engine.post(jam::golden::makeEvent(0, seq++, jam::EventType::SetInstrument, p, static_cast<uint8_t>(instrument)));
    const uint32_t here = std::min<uint32_t>(remaining, 8);
    for (uint32_t v = 0; v < here; ++v)
      h->engine.post(jam::golden::makeEvent(0, seq++, jam::EventType::NoteOn, p, static_cast<uint8_t>(48 + v * 3), 100));
    remaining -= here;
  }
  return h;
}

EMSCRIPTEN_KEEPALIVE void jam_bench_render(Host* h, uint32_t blocks) {
  for (uint32_t i = 0; i < blocks; ++i) h->engine.render(h->left, h->right, 128);
}

}  // extern "C"
