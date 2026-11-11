#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "core/rng.h"
#include "jam/engine.h"
#include "golden.h"

using namespace jam;

// Any heap use while this is armed is a real-time safety violation.
namespace {
std::atomic<bool> g_armed{false};
std::atomic<uint64_t> g_allocations{0};
}  // namespace

void* operator new(std::size_t n) {
  if (g_armed.load(std::memory_order_relaxed)) g_allocations.fetch_add(1, std::memory_order_relaxed);
  void* p = std::malloc(n);
  if (!p) std::abort();
  return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

using golden::makeEvent;
Event ev(int64_t tick, uint32_t seq, EventType type, uint8_t player, uint8_t a = 0, uint8_t b = 0, uint16_t c = 0) {
  return makeEvent(tick, seq, type, player, a, b, c);
}
uint64_t renderHash(uint32_t blockSize) { return golden::renderHash(48000, blockSize); }

}  // namespace

TEST_CASE("output does not depend on block size") {
  const uint64_t reference = renderHash(128);
  for (uint32_t size : {1u, 7u, 32u, 64u, 256u, 480u, 1024u, 4096u}) {
    INFO("block size " << size);
    REQUIRE(renderHash(size) == reference);
  }
}

TEST_CASE("rendering is repeatable") {
  REQUIRE(renderHash(128) == renderHash(128));
}

TEST_CASE("render never allocates") {
  auto engine = std::make_unique<Engine>();
  engine->prepare(48000);
  auto events = golden::script();
  std::vector<float> l(128), r(128);
  g_allocations = 0;
  g_armed = true;
  for (const auto& e : events) engine->post(e);
  for (int block = 0; block < 5000; ++block) engine->render(l.data(), r.data(), 128);
  g_armed = false;
  REQUIRE(g_allocations.load() == 0);
}

TEST_CASE("a note starts on exactly the scheduled sample") {
  Engine engine;
  engine.prepare(48000);
  const int64_t tick = 960;  // one beat at 120 BPM is exactly 24000 samples
  REQUIRE(engine.sampleAtTick(tick) == 24000);
  engine.post(ev(tick, 0, EventType::NoteOn, 0, 72, 127));
  std::vector<float> l(48000), r(48000);
  engine.render(l.data(), r.data(), 48000);
  for (int i = 0; i < 24000; ++i) REQUIRE(l[static_cast<size_t>(i)] == 0.0f);
  bool sounded = false;
  for (int i = 24000; i < 24100; ++i) sounded |= l[static_cast<size_t>(i)] != 0.0f;
  REQUIRE(sounded);
  REQUIRE(engine.stats().lateEvents == 0);
}

TEST_CASE("late events play immediately and are counted") {
  Engine engine;
  engine.prepare(48000);
  std::vector<float> l(4800), r(4800);
  engine.render(l.data(), r.data(), 4800);
  engine.post(ev(0, 0, EventType::NoteOn, 0, 60, 100));
  engine.render(l.data(), r.data(), 4800);
  REQUIRE(engine.stats().lateEvents == 1);
  REQUIRE(engine.stats().activeVoices >= 1);
}

TEST_CASE("voice stealing keeps polyphony within the patch limit") {
  Engine engine;
  engine.prepare(48000);
  engine.post(ev(0, 0, EventType::SetInstrument, 0, static_cast<uint8_t>(Instrument::Bass)));
  for (uint8_t n = 0; n < 10; ++n) engine.post(ev(0, n + 1u, EventType::NoteOn, 0, static_cast<uint8_t>(40 + n), 100));
  std::vector<float> l(128), r(128);
  engine.render(l.data(), r.data(), 128);
  engine.render(l.data(), r.data(), 128);
  REQUIRE(engine.stats().activeVoices == 2);
}

TEST_CASE("sequencer plays on grid and releases its notes") {
  Engine engine;
  engine.prepare(48000);
  engine.post(ev(0, 0, EventType::SetInstrument, 0, static_cast<uint8_t>(Instrument::Bass)));
  engine.post(ev(0, 1, EventType::SeqStep, 0, 4, 127, 48));
  std::vector<float> l(6000), r(6000);
  engine.render(l.data(), r.data(), 6000);  // 4 steps at 120 BPM is 24000 samples away
  for (float s : l) REQUIRE(s == 0.0f);
  std::vector<float> rest(24000), restR(24000);
  engine.render(rest.data(), restR.data(), 24000);
  double energy = 0;
  for (float s : rest) energy += static_cast<double>(s) * s;
  REQUIRE(energy > 0.1);
}

TEST_CASE("tick and sample conversions stay consistent, including after hours") {
  Engine engine;
  engine.prepare(44100, 1370);
  const int64_t sixHours = 960LL * 137 / 1 * 60 * 6;
  for (int64_t tick : {int64_t{0}, int64_t{1}, int64_t{240}, int64_t{960}, sixHours}) {
    const uint64_t s = engine.sampleAtTick(tick);
    REQUIRE(std::llabs(engine.tickAtSample(s + 1) - tick) <= 1);
  }
}

TEST_CASE("random event streams never produce NaN or leave the output range") {
  Engine engine;
  engine.prepare(48000);
  Rng rng(7);
  std::vector<float> l(256), r(256);
  uint32_t seq = 0;
  int64_t tick = 0;
  for (int block = 0; block < 4000; ++block) {
    for (uint32_t k = rng.next() % 4; k > 0; --k) {
      Event e;
      e.tick = tick + rng.next() % 2000;
      e.seq = seq++;
      e.type = static_cast<EventType>(rng.next() % 8);
      e.player = static_cast<uint8_t>(rng.next() % 12);  // includes invalid players
      e.a = static_cast<uint8_t>(rng.next());
      e.b = static_cast<uint8_t>(rng.next() % 128);
      e.c = static_cast<uint16_t>(rng.next());
      engine.post(e);
    }
    engine.render(l.data(), r.data(), 256);
    tick = engine.stats().tick;
    for (size_t i = 0; i < 256; ++i) {
      REQUIRE(std::isfinite(l[i]));
      REQUIRE(std::fabs(l[i]) <= 1.0f);
      REQUIRE(std::fabs(r[i]) <= 1.0f);
    }
  }
}

TEST_CASE("output matches the recorded golden hash") {
  const uint64_t h = golden::renderHash(48000, 128);
  INFO("hash 0x" << std::hex << h);
  REQUIRE(h == golden::kGoldenHash48k);
}
