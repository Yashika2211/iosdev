// Renders a short multi-instrument groove to a 16-bit stereo WAV file.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "jam/engine.h"

using namespace jam;

namespace {
uint32_t g_seq = 0;

void post(Engine& e, int64_t tick, EventType type, uint8_t player, uint8_t a = 0, uint8_t b = 0, uint16_t c = 0) {
  Event ev;
  ev.tick = tick;
  ev.seq = g_seq++;
  ev.type = type;
  ev.player = player;
  ev.a = a;
  ev.b = b;
  ev.c = c;
  e.post(ev);
}

void write32(FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
void write16(FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }
}  // namespace

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "jam_demo.wav";
  const uint32_t sr = 48000;
  auto engine = std::make_unique<Engine>();
  engine->prepare(sr);

  enum : uint8_t { kLead = 0, kBass = 1, kDrums = 2, kPads = 3 };
  // Drum pattern via the step sequencer.
  for (uint8_t s = 0; s < 16; s += 4) post(*engine, 0, EventType::SeqStep, kDrums, s, 125, 0);
  for (uint8_t s = 4; s < 16; s += 8) post(*engine, 0, EventType::SeqStep, kDrums, s, 110, 1);
  for (uint8_t s = 2; s < 16; s += 4) post(*engine, 0, EventType::SeqStep, kDrums, s, 85, 2);

  const int64_t bar = kTicksPerQuarter * 4;
  const uint8_t roots[4] = {33, 29, 36, 31};              // A F C G
  const uint8_t chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {55, 60, 64}, {55, 59, 62}};
  const uint8_t melody[8] = {76, 72, 69, 72, 74, 72, 67, 69};
  const int bars = 8;

  for (int b = 0; b < bars; ++b) {
    const int64_t start = b * bar;
    const int ch = b % 4;
    for (uint8_t note : chords[ch]) {
      post(*engine, start, EventType::NoteOn, kPads, note, 90);
      post(*engine, start + bar - 60, EventType::NoteOff, kPads, note);
    }
    for (int i = 0; i < 8; ++i) {
      const int64_t t = start + i * kTicksPerQuarter / 2;
      const auto note = static_cast<uint8_t>(roots[ch] + ((i % 4 == 3) ? 12 : 0));
      post(*engine, t, EventType::NoteOn, kBass, note, 110);
      post(*engine, t + kTicksPerQuarter / 3, EventType::NoteOff, kBass, note);
    }
    if (b >= 2) {
      for (int i = 0; i < 8; ++i) {
        if ((i + b) % 3 == 0) continue;
        const int64_t t = start + i * kTicksPerQuarter / 2;
        post(*engine, t, EventType::NoteOn, kLead, melody[(i + b) % 8], 100);
        post(*engine, t + kTicksPerQuarter / 3, EventType::NoteOff, kLead, melody[(i + b) % 8]);
      }
    }
    // Open the bass filter across the bars, as a player would with the XY pad.
    post(*engine, start, EventType::SetParam, kBass, 0, 0, static_cast<uint16_t>(14000 + b * 3500));
  }

  const uint32_t frames = static_cast<uint32_t>(engine->sampleAtTick(bars * bar)) + sr * 2;
  std::vector<int16_t> pcm;
  pcm.reserve(frames * 2);
  std::vector<float> l(128), r(128);
  double sumSquares = 0;
  float peak = 0;
  for (uint32_t done = 0; done < frames; done += 128) {
    engine->render(l.data(), r.data(), 128);
    for (int i = 0; i < 128; ++i) {
      for (float s : {l[static_cast<size_t>(i)], r[static_cast<size_t>(i)]}) {
        peak = std::max(peak, std::fabs(s));
        sumSquares += static_cast<double>(s) * s;
        pcm.push_back(static_cast<int16_t>(std::lrint(s * 32767.0f)));
      }
    }
  }

  FILE* f = std::fopen(path, "wb");
  if (!f) {
    std::perror(path);
    return 1;
  }
  const auto bytes = static_cast<uint32_t>(pcm.size() * 2);
  std::fwrite("RIFF", 1, 4, f);
  write32(f, 36 + bytes);
  std::fwrite("WAVEfmt ", 1, 8, f);
  write32(f, 16);
  write16(f, 1);
  write16(f, 2);
  write32(f, sr);
  write32(f, sr * 4);
  write16(f, 4);
  write16(f, 16);
  std::fwrite("data", 1, 4, f);
  write32(f, bytes);
  std::fwrite(pcm.data(), 2, pcm.size(), f);
  std::fclose(f);

  const auto s = engine->stats();
  std::printf("wrote %s: %.1f s, peak %.3f, rms %.3f, late %u, dropped %u\n", path,
              static_cast<double>(frames) / sr, peak, std::sqrt(sumSquares / static_cast<double>(pcm.size())),
              s.lateEvents, s.droppedEvents);
  return 0;
}
