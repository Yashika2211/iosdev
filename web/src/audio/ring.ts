// Single-producer single-consumer event ring in a SharedArrayBuffer. The UI
// thread writes, the audio thread reads; neither blocks nor allocates.
export const RING_SLOTS = 1024; // power of two
export const SLOT_WORDS = 5;
const HEADER_WORDS = 2; // [write index, read index]

export function ringByteLength(): number {
  return (HEADER_WORDS + RING_SLOTS * SLOT_WORDS) * 4;
}

export class EventRing {
  private readonly words: Int32Array;

  constructor(buffer: SharedArrayBuffer) {
    this.words = new Int32Array(buffer);
  }

  // Producer side. `packed` holds SLOT_WORDS words starting at `offset`.
  push(packed: Int32Array, offset = 0): boolean {
    const w = Atomics.load(this.words, 0);
    const r = Atomics.load(this.words, 1);
    if (((w - r) | 0) >= RING_SLOTS) return false;
    const base = HEADER_WORDS + (w & (RING_SLOTS - 1)) * SLOT_WORDS;
    for (let i = 0; i < SLOT_WORDS; i++) this.words[base + i] = packed[offset + i];
    Atomics.store(this.words, 0, (w + 1) | 0);
    return true;
  }

  // Consumer side. Copies one event into `out`; false when empty.
  pop(out: Int32Array): boolean {
    const r = Atomics.load(this.words, 1);
    if (r === Atomics.load(this.words, 0)) return false;
    const base = HEADER_WORDS + (r & (RING_SLOTS - 1)) * SLOT_WORDS;
    for (let i = 0; i < SLOT_WORDS; i++) out[i] = this.words[base + i];
    Atomics.store(this.words, 1, (r + 1) | 0);
    return true;
  }
}

// Stats the audio thread publishes for the UI, one Float64 each.
export const Stat = {
  Tick: 0,
  SamplePosition: 1,
  ActiveVoices: 2,
  LateEvents: 3,
  DroppedEvents: 4,
  Peak: 5,
  LoadPercent: 6, // render time / real time over the last second
  Blocks: 7,
  Updated: 8, // AudioContext time of the last update, seconds
} as const;
export const STAT_COUNT = 9;
