// Main-thread side of the audio engine: creates the AudioContext, loads the
// worklet and wasm, and forwards events to the audio thread.
import workletUrl from './worklet.ts?worker&url';
import { type JamEvent, packEvent } from './events';
import { EventRing, SLOT_WORDS, STAT_COUNT, Stat, ringByteLength } from './ring';
import type { ProcessorOptions } from './worklet';

export interface AudioStats {
  tick: number;
  samplePosition: number;
  activeVoices: number;
  lateEvents: number;
  droppedEvents: number;
  peak: number;
  loadPercent: number;
  blocks: number;
  sampleRate: number;
  baseLatencyMs: number;
  outputLatencyMs: number;
  shared: boolean;
}

type Navigator17 = Navigator & { audioSession?: { type: string } };

export class JamAudio {
  private readonly packed = new Int32Array(SLOT_WORDS);
  private seq = 1;
  private goldenWaiter: ((hash: string) => void) | null = null;
  private portStats = new Float64Array(STAT_COUNT);

  private constructor(
    readonly context: AudioContext,
    private readonly node: AudioWorkletNode,
    private readonly ring: EventRing | null,
    private readonly stats: Float64Array | null,
  ) {
    node.port.onmessage = (e: MessageEvent) => {
      const msg = e.data as { type: string; hash?: string; stats?: Float64Array };
      if (msg.type === 'golden' && this.goldenWaiter) this.goldenWaiter(msg.hash!);
      if (msg.type === 'stats' && msg.stats) this.portStats = msg.stats;
    };
  }

  // Must be called from a user gesture (a tap or click), or browsers keep
  // the context suspended.
  static async start(bpmTimes10 = 1200, wasmUrl = '/jam.wasm'): Promise<JamAudio> {
    // iOS Safari mutes Web Audio when the ring/silent switch is on unless the
    // session is marked as playback.
    const nav = navigator as Navigator17;
    if (nav.audioSession) nav.audioSession.type = 'playback';

    const context = new AudioContext({ latencyHint: 'interactive' });
    const resumed = context.resume();
    const [, wasmBytes] = await Promise.all([
      context.audioWorklet.addModule(workletUrl),
      fetch(wasmUrl).then((r) => {
        if (!r.ok) throw new Error(`failed to load ${wasmUrl}: ${r.status}`);
        return r.arrayBuffer();
      }),
    ]);

    const shared = globalThis.crossOriginIsolated === true;
    const ringBuffer = shared ? new SharedArrayBuffer(ringByteLength()) : null;
    const statsBuffer = shared ? new SharedArrayBuffer(STAT_COUNT * 8) : null;
    const processorOptions: ProcessorOptions = { wasmBytes, bpmTimes10, ringBuffer, statsBuffer };
    const node = new AudioWorkletNode(context, 'jam', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
      processorOptions,
    });
    const ready = new Promise<void>((resolve) => {
      node.port.addEventListener('message', function onReady(e: MessageEvent) {
        if ((e.data as { type: string }).type === 'ready') {
          node.port.removeEventListener('message', onReady);
          resolve();
        }
      });
      node.port.start();
    });
    node.connect(context.destination);
    await ready;
    await resumed;
    return new JamAudio(
      context,
      node,
      ringBuffer ? new EventRing(ringBuffer) : null,
      statsBuffer ? new Float64Array(statsBuffer) : null,
    );
  }

  nextSeq(): number {
    return this.seq++;
  }

  post(event: Omit<JamEvent, 'seq'> & { seq?: number }): boolean {
    const e: JamEvent = { ...event, seq: event.seq ?? this.nextSeq() };
    if (this.ring) {
      packEvent(e, this.packed);
      return this.ring.push(this.packed);
    }
    const words = new Int32Array(SLOT_WORDS);
    packEvent(e, words);
    this.node.port.postMessage({ type: 'events', events: words });
    return true;
  }

  seek(tick: number): void {
    this.node.port.postMessage({ type: 'seek', tick });
  }

  readStats(): AudioStats {
    const s = this.stats ?? this.portStats;
    return {
      tick: s[Stat.Tick],
      samplePosition: s[Stat.SamplePosition],
      activeVoices: s[Stat.ActiveVoices],
      lateEvents: s[Stat.LateEvents],
      droppedEvents: s[Stat.DroppedEvents],
      peak: s[Stat.Peak],
      loadPercent: s[Stat.LoadPercent],
      blocks: s[Stat.Blocks],
      sampleRate: this.context.sampleRate,
      baseLatencyMs: (this.context.baseLatency ?? 0) * 1000,
      outputLatencyMs: (this.context.outputLatency ?? 0) * 1000,
      shared: this.stats !== null,
    };
  }

  // Renders the golden script inside the worklet's own wasm instance.
  golden(sampleRate = 48000, blockSize = 128): Promise<string> {
    return new Promise((resolve) => {
      this.goldenWaiter = resolve;
      this.node.port.postMessage({ type: 'golden', sampleRate, blockSize });
    });
  }

  async close(): Promise<void> {
    this.node.disconnect();
    await this.context.close();
  }
}
