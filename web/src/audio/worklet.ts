// Runs on the audio thread. Instantiates the engine from bytes the page sends,
// drains queued events and renders each 128-frame quantum.
import { EventRing, SLOT_WORDS, STAT_COUNT, Stat } from './ring';

interface EngineExports {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  jam_create(sampleRate: number, bpmTimes10: number): number;
  jam_post(h: number, tickLo: number, tickHi: number, seq: number, packed: number, c: number): number;
  jam_seek(h: number, tickLo: number, tickHi: number): void;
  jam_render(h: number, frames: number): void;
  jam_left(h: number): number;
  jam_right(h: number): number;
  jam_take_peak(h: number): number;
  jam_active_voices(h: number): number;
  jam_late_events(h: number): number;
  jam_dropped_events(h: number): number;
  jam_tick(h: number): number;
  jam_sample_position(h: number): number;
  jam_golden_hash(sampleRate: number, blockSize: number): bigint;
}

export interface ProcessorOptions {
  wasmBytes: ArrayBuffer;
  bpmTimes10: number;
  ringBuffer: SharedArrayBuffer | null;
  statsBuffer: SharedArrayBuffer | null;
}

// performance.now() is missing from some worklet scopes; Date.now() is coarser
// but averages out over a one-second window.
const now: () => number =
  typeof globalThis.performance?.now === 'function' ? () => globalThis.performance.now() : () => Date.now();

class JamProcessor extends AudioWorkletProcessor {
  private readonly ex: EngineExports;
  private readonly h: number;
  private readonly left: Float32Array;
  private readonly right: Float32Array;
  private readonly ring: EventRing | null;
  private readonly stats: Float64Array;
  private readonly sharedStats: boolean;
  private readonly slot = new Int32Array(SLOT_WORDS);
  private blocks = 0;
  private windowRenderMs = 0;
  private windowFrames = 0;
  private load = 0;
  private peak = 0;

  constructor(options: AudioWorkletNodeOptions) {
    super();
    const opts = options.processorOptions as ProcessorOptions;
    const module = new WebAssembly.Module(opts.wasmBytes);
    const instance = new WebAssembly.Instance(module, {});
    this.ex = instance.exports as unknown as EngineExports;
    this.ex._initialize?.();
    this.h = this.ex.jam_create(sampleRate, opts.bpmTimes10);
    // Memory never grows (ALLOW_MEMORY_GROWTH=0), so these views stay valid.
    this.left = new Float32Array(this.ex.memory.buffer, this.ex.jam_left(this.h), 1024);
    this.right = new Float32Array(this.ex.memory.buffer, this.ex.jam_right(this.h), 1024);
    this.ring = opts.ringBuffer ? new EventRing(opts.ringBuffer) : null;
    this.sharedStats = opts.statsBuffer !== null;
    this.stats = opts.statsBuffer ? new Float64Array(opts.statsBuffer) : new Float64Array(STAT_COUNT);
    this.port.onmessage = (e: MessageEvent) => this.onMessage(e.data);
    this.port.postMessage({ type: 'ready', sampleRate, shared: this.sharedStats });
  }

  private onMessage(msg: { type: string; events?: Int32Array; tick?: number; sampleRate?: number; blockSize?: number }): void {
    switch (msg.type) {
      case 'events': {
        // Fallback path when SharedArrayBuffer is unavailable.
        const ev = msg.events!;
        for (let i = 0; i + SLOT_WORDS <= ev.length; i += SLOT_WORDS)
          this.ex.jam_post(this.h, ev[i], ev[i + 1], ev[i + 2], ev[i + 3], ev[i + 4]);
        break;
      }
      case 'seek': {
        const t = msg.tick!;
        const hi = Math.floor(t / 4294967296);
        this.ex.jam_seek(this.h, (t - hi * 4294967296) | 0, hi | 0);
        break;
      }
      case 'golden': {
        // Diagnostic: run the golden script on this thread's wasm instance.
        const hash = BigInt.asUintN(64, this.ex.jam_golden_hash(msg.sampleRate ?? 48000, msg.blockSize ?? 128));
        this.port.postMessage({ type: 'golden', hash: hash.toString(16) });
        break;
      }
    }
  }

  process(_inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    const ex = this.ex;
    const h = this.h;
    if (this.ring) {
      const slot = this.slot;
      while (this.ring.pop(slot)) ex.jam_post(h, slot[0], slot[1], slot[2], slot[3], slot[4]);
    }

    const out = outputs[0];
    const frames = out[0].length;
    const t0 = now();
    ex.jam_render(h, frames);
    out[0].set(this.left.subarray(0, frames));
    if (out.length > 1) out[1].set(this.right.subarray(0, frames));
    this.windowRenderMs += now() - t0;
    this.windowFrames += frames;
    this.blocks++;

    if (this.windowFrames >= sampleRate) {
      this.load = (this.windowRenderMs / ((this.windowFrames / sampleRate) * 1000)) * 100;
      this.windowRenderMs = 0;
      this.windowFrames = 0;
    }
    this.peak = Math.max(this.peak * 0.97, ex.jam_take_peak(h));

    // Publish stats about every 23 ms at 44.1-48 kHz.
    if ((this.blocks & 7) === 0) {
      const s = this.stats;
      s[Stat.Tick] = ex.jam_tick(h);
      s[Stat.SamplePosition] = ex.jam_sample_position(h);
      s[Stat.ActiveVoices] = ex.jam_active_voices(h);
      s[Stat.LateEvents] = ex.jam_late_events(h);
      s[Stat.DroppedEvents] = ex.jam_dropped_events(h);
      s[Stat.Peak] = this.peak;
      s[Stat.LoadPercent] = this.load;
      s[Stat.Blocks] = this.blocks;
      s[Stat.Updated] = currentTime;
      if (!this.sharedStats && (this.blocks & 31) === 0) this.port.postMessage({ type: 'stats', stats: s });
    }
    return true;
  }
}

registerProcessor('jam', JamProcessor);
