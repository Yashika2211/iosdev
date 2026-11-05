// The public engine API. It has no platform code; the web, iOS and native hosts
// all call the same class.
//
// Threads: prepare() on any thread before audio starts. post() from exactly one
// non-audio thread. render() from the audio thread; it never allocates, locks or
// makes system calls.
#pragma once
#include <cstdint>
#include <memory>

#include "jam/events.h"

namespace jam {

struct EngineStats {
  int activeVoices = 0;
  uint32_t lateEvents = 0;     // events that arrived after their scheduled time
  uint32_t droppedEvents = 0;  // queue or schedule full
  int64_t tick = 0;
  uint64_t samplePosition = 0;
};

class Engine {
 public:
  Engine();
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void prepare(uint32_t sampleRate, uint32_t bpmTimes10 = 1200);

  // Moves the playhead to a grid position, for example when joining a room.
  // Call before audio starts, or from the audio thread between renders.
  void seek(int64_t tick);

  // Queues an event for the audio thread. Returns false if the queue is full.
  bool post(const Event& event);

  // Fills `frames` samples of stereo audio, sample-accurately applying events.
  void render(float* left, float* right, uint32_t frames);

  int64_t tickAtSample(uint64_t sample) const;
  uint64_t sampleAtTick(int64_t tick) const;
  uint32_t sampleRate() const;
  EngineStats stats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace jam
