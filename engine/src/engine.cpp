#include "jam/engine.h"

#include <array>
#include <cstring>

#include "core/detmath.h"
#include "core/spsc_queue.h"
#include "drums/drums.h"
#include "dsp/voice.h"
#include "fx/compressor.h"
#include "fx/delay.h"
#include "fx/reverb.h"
#include "seq/sequencer.h"

namespace jam {
namespace {

constexpr int kVoicesPerPlayer = 12;
constexpr int kMaxPending = 1024;
constexpr uint32_t kInternalSeqBase = 0x80000000u;
constexpr int64_t kSeqGateTicks = kTicksPerStep / 2;

Patch makePatch(Instrument instrument) {
  Patch p;
  switch (instrument) {
    case Instrument::Lead:
      p.wave1 = Waveform::Saw;
      p.wave2 = Waveform::Square;
      p.detuneSemis = 0.08f;
      p.mix2 = 0.35f;
      p.amp = {0.003f, 0.18f, 0.6f, 0.18f};
      p.filter = {0.002f, 0.2f, 0.25f, 0.2f};
      p.filterEnvOctaves = 2.5f;
      p.lfoHz = 5.0f;
      p.lfoOctaves = 0.08f;
      p.gain = 0.28f;
      p.polyphony = 8;
      p.delaySend = 0.25f;
      p.reverbSend = 0.15f;
      break;
    case Instrument::Bass:
      p.wave1 = Waveform::Square;
      p.wave2 = Waveform::Saw;
      p.detuneSemis = 0.05f;
      p.mix2 = 0.4f;
      p.subLevel = 0.6f;
      p.amp = {0.002f, 0.15f, 0.5f, 0.06f};
      p.filter = {0.002f, 0.12f, 0.1f, 0.08f};
      p.filterEnvOctaves = 2.0f;
      p.gain = 0.4f;
      p.polyphony = 2;
      p.reverbSend = 0.02f;
      break;
    case Instrument::Pads:
      p.wave1 = Waveform::Wavetable;
      p.wave2 = Waveform::Saw;
      p.detuneSemis = 0.12f;
      p.mix2 = 0.35f;
      p.amp = {0.4f, 0.8f, 0.75f, 0.9f};
      p.filter = {0.6f, 1.0f, 0.5f, 0.9f};
      p.filterEnvOctaves = 1.0f;
      p.lfoHz = 0.25f;
      p.lfoOctaves = 0.4f;
      p.gain = 0.16f;
      p.polyphony = 8;
      p.delaySend = 0.1f;
      p.reverbSend = 0.45f;
      break;
    case Instrument::Drums:
      p.delaySend = 0.02f;
      p.reverbSend = 0.06f;
      break;
  }
  return p;
}

struct Player {
  Instrument instrument = Instrument::Lead;
  std::array<Voice, kVoicesPerPlayer> voices;
  DrumKit drums;
  StepSequencer seq;
  float cutoffTarget = 0.65f, cutoff = 0.65f;
  float resonanceTarget = 0.2f, resonance = 0.2f;
  float panL = 0.7071f, panR = 0.7071f;
  bool anyAudible = false;
};

}  // namespace

struct Engine::Impl {
  float sr = 48000.0f;
  uint32_t srInt = 48000;
  uint32_t bpm10 = 1200;

  // Tempo changes rebase the tick <-> sample mapping at this anchor.
  int64_t originTick = 0;
  uint64_t originSample = 0;
  uint64_t pos = 0;
  int64_t nextStep = 0;

  WavetableBank bank;
  std::array<Patch, 4> patches;
  std::array<Player, kMaxPlayers> players;
  PingPongDelay delay;
  FdnReverb reverb;
  Compressor comp;

  SpscQueue<Event, 2048> inbox;
  std::array<Event, kMaxPending> pending;
  int pendingCount = 0;

  uint64_t voiceOrder = 0;
  uint32_t internalSeq = kInternalSeqBase;
  uint32_t late = 0, dropped = 0;
  int activeVoices = 0;

  // sample = origin + (tick - originTick) * sr * 600 / (bpm10 * ppq).
  // 128-bit intermediates keep this exact for any realistic session length.
  uint64_t sampleAt(int64_t tick) const {
    const int64_t dt = tick - originTick;
    const auto denom = static_cast<unsigned __int128>(bpm10) * kTicksPerQuarter;
    if (dt <= 0) {
      const auto back = static_cast<unsigned __int128>(-dt) * srInt * 600 / denom;
      return static_cast<uint64_t>(back) > originSample ? 0 : originSample - static_cast<uint64_t>(back);
    }
    return originSample + static_cast<uint64_t>(static_cast<unsigned __int128>(dt) * srInt * 600 / denom);
  }

  int64_t tickAt(uint64_t sample) const {
    const uint64_t ds = sample - originSample;
    const auto num = static_cast<unsigned __int128>(ds) * bpm10 * kTicksPerQuarter;
    return originTick + static_cast<int64_t>(num / (static_cast<unsigned __int128>(srInt) * 600));
  }

  void schedule(const Event& e) {
    if (pendingCount == kMaxPending) {
      ++dropped;
      return;
    }
    int i = pendingCount;
    while (i > 0 && eventBefore(e, pending[static_cast<size_t>(i - 1)])) {
      pending[static_cast<size_t>(i)] = pending[static_cast<size_t>(i - 1)];
      --i;
    }
    pending[static_cast<size_t>(i)] = e;
    ++pendingCount;
  }

  void noteOn(Player& pl, uint8_t note, uint8_t velocity) {
    if (pl.instrument == Instrument::Drums) {
      pl.drums.trigger(note, velocity);
      return;
    }
    const Patch& patch = patches[static_cast<size_t>(pl.instrument)];
    Voice* chosen = nullptr;
    // A free voice first, otherwise steal the oldest. `order` is a counter
    // rather than a timer, so stealing is deterministic.
    int inUse = 0;
    Voice* oldest = nullptr;
    for (auto& v : pl.voices) {
      if (!v.active()) {
        if (!chosen) chosen = &v;
      } else {
        ++inUse;
        if (!oldest || v.order() < oldest->order()) oldest = &v;
      }
    }
    if (inUse >= patch.polyphony || !chosen) chosen = oldest;
    chosen->noteOn(patch, note, velocity, voiceOrder++);
  }

  void apply(const Event& e) {
    if (e.player >= kMaxPlayers && e.type != EventType::Tempo) return;
    Player& pl = players[e.player < kMaxPlayers ? e.player : 0];
    switch (e.type) {
      case EventType::NoteOn:
        if (e.b > 0) noteOn(pl, e.a, e.b);
        break;
      case EventType::NoteOff:
        for (auto& v : pl.voices)
          if (v.active() && v.gate() && v.note() == e.a) v.noteOff();
        break;
      case EventType::SetParam: {
        const float value = static_cast<float>(e.c) / 65535.0f;
        if (e.a == static_cast<uint8_t>(Param::Cutoff)) pl.cutoffTarget = value;
        else if (e.a == static_cast<uint8_t>(Param::Resonance)) pl.resonanceTarget = value;
        break;
      }
      case EventType::SetInstrument:
        for (auto& v : pl.voices)
          if (v.active()) v.noteOff();
        if (e.a < 4) pl.instrument = static_cast<Instrument>(e.a);
        break;
      case EventType::SeqStep:
        pl.seq.set(e.a, e.c, e.b);
        break;
      case EventType::SeqClear:
        pl.seq.clear();
        break;
      case EventType::Tempo:
        if (e.c >= 200) {
          originSample = pos;
          originTick = tickAt(pos);
          bpm10 = e.c;
          // Hold the delay at a dotted 8th of the new tempo.
          delay.setTime(45.0f / (static_cast<float>(bpm10) * 0.1f));
        }
        break;
      case EventType::AllNotesOff:
        for (auto& v : pl.voices)
          if (v.active()) v.noteOff();
        break;
    }
  }

  void drainInbox() {
    Event e;
    while (inbox.pop(e)) schedule(e);
  }

  void applyDue() {
    int done = 0;
    while (done < pendingCount) {
      const Event& e = pending[static_cast<size_t>(done)];
      const uint64_t at = sampleAt(e.tick);
      if (at > pos) break;
      if (at < pos && e.seq < kInternalSeqBase) ++late;
      apply(e);
      ++done;
    }
    if (done > 0) {
      for (int i = done; i < pendingCount; ++i)
        pending[static_cast<size_t>(i - done)] = pending[static_cast<size_t>(i)];
      pendingCount -= done;
    }
  }

  void fireSteps() {
    while (sampleAt(nextStep * kTicksPerStep) <= pos) {
      const int64_t tick = nextStep * kTicksPerStep;
      for (int p = 0; p < kMaxPlayers; ++p) {
        const auto& step = players[static_cast<size_t>(p)].seq.at(nextStep);
        if (step.velocity == 0) continue;
        const auto note = static_cast<uint8_t>(step.note);
        noteOn(players[static_cast<size_t>(p)], note, step.velocity);
        Event off;
        off.tick = tick + kSeqGateTicks;
        off.seq = internalSeq++;
        off.type = EventType::NoteOff;
        off.player = static_cast<uint8_t>(p);
        off.a = note;
        schedule(off);
      }
      ++nextStep;
    }
  }

  void control() {
    int count = 0;
    for (auto& pl : players) {
      pl.cutoff += (pl.cutoffTarget - pl.cutoff) * 0.2f;
      pl.resonance += (pl.resonanceTarget - pl.resonance) * 0.2f;
      const float hz = 80.0f * dm::pow2(pl.cutoff * 7.2f);
      for (auto& v : pl.voices) {
        if (v.active()) {
          v.control(hz, pl.resonance);
          ++count;
        }
      }
      count += pl.drums.activeCount();
    }
    activeVoices = count;
  }

  void renderChunk(float* outL, float* outR, int n) {
    std::array<float, Voice::kControlInterval> dryL{}, dryR{}, delaySend{}, verbSend{};
    for (auto& pl : players) {
      const Patch& patch = patches[static_cast<size_t>(pl.instrument)];
      for (auto& v : pl.voices) {
        if (!v.active()) continue;
        for (int i = 0; i < n; ++i) {
          const float s = v.next();
          const auto u = static_cast<size_t>(i);
          dryL[u] += s * pl.panL;
          dryR[u] += s * pl.panR;
          delaySend[u] += s * patch.delaySend;
          verbSend[u] += s * patch.reverbSend;
        }
      }
      if (pl.drums.activeCount() > 0) {
        for (int i = 0; i < n; ++i) {
          const float s = pl.drums.next();
          const auto u = static_cast<size_t>(i);
          dryL[u] += s * pl.panL;
          dryR[u] += s * pl.panR;
          delaySend[u] += s * patch.delaySend;
          verbSend[u] += s * patch.reverbSend;
        }
      }
    }
    for (int i = 0; i < n; ++i) {
      const auto u = static_cast<size_t>(i);
      float dl, dr, rl, rr;
      delay.process(delaySend[u], dl, dr);
      reverb.process(verbSend[u], rl, rr);
      float l = dryL[u] + dl + rl;
      float r = dryR[u] + dr + rr;
      comp.process(l, r);
      outL[i] = dm::softClip(l);
      outR[i] = dm::softClip(r);
    }
  }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare(uint32_t sampleRate, uint32_t bpmTimes10) {
  Impl& m = *impl_;
  m.srInt = sampleRate;
  m.sr = static_cast<float>(sampleRate);
  m.bpm10 = bpmTimes10;
  m.bank.build();
  for (int i = 0; i < 4; ++i) m.patches[static_cast<size_t>(i)] = makePatch(static_cast<Instrument>(i));
  for (int p = 0; p < kMaxPlayers; ++p) {
    Player& pl = m.players[static_cast<size_t>(p)];
    pl.instrument = static_cast<Instrument>(p % 4);
    for (auto& v : pl.voices) v.prepare(&m.bank, m.sr);
    pl.drums.prepare(m.sr);
    // Spread the players across the stereo field with an equal-power law.
    const float position = (static_cast<float>(p) + 0.5f) / kMaxPlayers;
    const float angle = (0.25f + position * 0.5f) * 0.25f;
    pl.panL = dm::cosTurns(angle);
    pl.panR = dm::sinTurns(angle);
  }
  m.delay.prepare(m.sr);
  m.delay.setTime(45.0f / (static_cast<float>(bpmTimes10) * 0.1f));
  m.reverb.prepare(m.sr);
  m.comp.prepare(m.sr);
}

void Engine::seek(int64_t tick) {
  Impl& m = *impl_;
  m.originTick = tick;
  m.originSample = m.pos;
  const int64_t rem = tick % kTicksPerStep;
  m.nextStep = tick / kTicksPerStep + (rem > 0 ? 1 : 0);
}

bool Engine::post(const Event& event) {
  if (impl_->inbox.push(event)) return true;
  return false;
}

void Engine::render(float* left, float* right, uint32_t frames) {
  Impl& m = *impl_;
  m.drainInbox();
  uint32_t done = 0;
  while (done < frames) {
    m.applyDue();
    m.fireSteps();
    // fireSteps can schedule a note-off that is already due at this sample.
    m.applyDue();
    const uint64_t interval = Voice::kControlInterval;
    if (m.pos % interval == 0) m.control();

    uint64_t next = m.pos - (m.pos % interval) + interval;
    if (m.pendingCount > 0) {
      const uint64_t at = m.sampleAt(m.pending[0].tick);
      if (at < next) next = at;
    }
    const uint64_t stepAt = m.sampleAt(m.nextStep * kTicksPerStep);
    if (stepAt < next) next = stepAt;

    uint64_t n = next - m.pos;
    if (n > frames - done) n = frames - done;
    m.renderChunk(left + done, right + done, static_cast<int>(n));
    m.pos += n;
    done += static_cast<uint32_t>(n);
  }
}

int64_t Engine::tickAtSample(uint64_t sample) const { return impl_->tickAt(sample); }
uint64_t Engine::sampleAtTick(int64_t tick) const { return impl_->sampleAt(tick); }
uint32_t Engine::sampleRate() const { return impl_->srInt; }

EngineStats Engine::stats() const {
  const Impl& m = *impl_;
  EngineStats s;
  s.activeVoices = m.activeVoices;
  s.lateEvents = m.late;
  s.droppedEvents = m.dropped;
  s.samplePosition = m.pos;
  s.tick = m.tickAt(m.pos);
  return s;
}

}  // namespace jam
