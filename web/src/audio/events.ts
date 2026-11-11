// Mirrors engine/include/jam/events.h. Keep the two in step.
export const TICKS_PER_QUARTER = 960;
export const TICKS_PER_STEP = TICKS_PER_QUARTER / 4;

export const EventType = {
  NoteOn: 0,
  NoteOff: 1,
  SetParam: 2,
  SetInstrument: 3,
  SeqStep: 4,
  SeqClear: 5,
  Tempo: 6,
  AllNotesOff: 7,
} as const;
export type EventType = (typeof EventType)[keyof typeof EventType];

export const Instrument = { Lead: 0, Bass: 1, Drums: 2, Pads: 3 } as const;
export type Instrument = (typeof Instrument)[keyof typeof Instrument];

export const Param = { Cutoff: 0, Resonance: 1 } as const;

export interface JamEvent {
  tick: number;
  seq: number;
  type: EventType;
  player: number;
  a?: number;
  b?: number;
  c?: number;
}

const TWO_32 = 4294967296;

// The wasm API takes events as five 32-bit words.
export function packEvent(e: JamEvent, out: Int32Array, offset = 0): void {
  const hi = Math.floor(e.tick / TWO_32);
  out[offset] = (e.tick - hi * TWO_32) | 0;
  out[offset + 1] = hi | 0;
  out[offset + 2] = e.seq | 0;
  out[offset + 3] = (e.type & 0xff) | ((e.player & 0xff) << 8) | (((e.a ?? 0) & 0xff) << 16) | (((e.b ?? 0) & 0xff) << 24);
  out[offset + 4] = (e.c ?? 0) & 0xffff;
}

// First grid line at least `lookahead` ticks after `now`. Same as the C++ one.
export function scheduleTick(now: number, lookahead: number, grid = TICKS_PER_STEP): number {
  const t = now + lookahead;
  const rem = ((t % grid) + grid) % grid;
  return rem === 0 ? t : t + grid - rem;
}
