// Measures wasm CPU cost per voice on the main thread, the same way
// engine/bench does natively: extra time of N voices over an idle engine.
interface BenchExports {
  _initialize?: () => void;
  jam_bench_create(instrument: number, voices: number): number;
  jam_bench_render(h: number, blocks: number): void;
  jam_destroy(h: number): void;
}

export interface BenchResult {
  instrument: string;
  voices: number;
  totalPercentOfCore: number;
  percentPerVoice: number;
  nsPerVoiceSample: number;
}

export async function runBench(wasmUrl = '/jam.wasm', seconds = 10, repeats = 5): Promise<{ idlePercent: number; results: BenchResult[] }> {
  const bytes = await (await fetch(wasmUrl)).arrayBuffer();
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const ex = instance.exports as unknown as BenchExports;
  ex._initialize?.();

  const rate = 48000;
  const blocks = Math.floor((rate * seconds) / 128);
  const time = (instrument: number, voices: number): number => {
    let best = Infinity;
    for (let i = 0; i < repeats; i++) {
      const h = ex.jam_bench_create(instrument, voices);
      ex.jam_bench_render(h, Math.floor(rate / 128)); // past the attack
      const t0 = performance.now();
      ex.jam_bench_render(h, blocks);
      best = Math.min(best, performance.now() - t0);
      ex.jam_destroy(h);
    }
    return best / 1000;
  };

  const idle = time(0, 0);
  const cases: [string, number, number][] = [
    ['lead', 0, 8],
    ['lead', 0, 32],
    ['pads', 3, 8],
    ['pads', 3, 32],
  ];
  const results = cases.map(([name, instrument, voices]) => {
    const t = time(instrument, voices);
    const perVoice = (t - idle) / voices;
    return {
      instrument: name,
      voices,
      totalPercentOfCore: (t / seconds) * 100,
      percentPerVoice: (perVoice / seconds) * 100,
      nsPerVoiceSample: (perVoice * 1e9) / (rate * seconds),
    };
  });
  return { idlePercent: (idle / seconds) * 100, results };
}
