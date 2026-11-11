import { JamAudio } from './audio/engine-host';
import { runBench } from './audio/bench';
import { EventType, Instrument } from './audio/events';

const NOTES = [60, 62, 64, 67, 69, 72, 74, 76, 79, 81, 84, 86];
const DRUMS = ['Kick', 'Snare', 'Hat', 'Open hat'];
const PLAYER = 0;

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
let audio: JamAudio | null = null;
let instrument: number = Instrument.Lead;

// Exposed for the Playwright tests and for poking at from the console.
const api = {
  ready: false,
  get audio() {
    return audio;
  },
  noteOn(note: number, velocity = 110) {
    audio?.post({ tick: audio.readStats().tick, type: EventType.NoteOn, player: PLAYER, a: note, b: velocity });
  },
  noteOff(note: number) {
    audio?.post({ tick: audio.readStats().tick, type: EventType.NoteOff, player: PLAYER, a: note });
  },
  setInstrument(i: number) {
    instrument = i;
    audio?.post({ tick: 0, type: EventType.SetInstrument, player: PLAYER, a: i });
    renderPads();
  },
  runBench,
};
(window as unknown as { jam: typeof api }).jam = api;

function renderPads(): void {
  const pads = $('pads');
  pads.replaceChildren();
  const labels = instrument === Instrument.Drums ? DRUMS : NOTES.map(String);
  labels.forEach((label, i) => {
    const pad = document.createElement('div');
    pad.className = 'pad';
    pad.textContent = label;
    const note = instrument === Instrument.Drums ? i : NOTES[i];
    pad.addEventListener('pointerdown', (e) => {
      pad.setPointerCapture(e.pointerId);
      pad.classList.add('on');
      api.noteOn(note);
    });
    const release = () => {
      if (!pad.classList.contains('on')) return;
      pad.classList.remove('on');
      api.noteOff(note);
    };
    pad.addEventListener('pointerup', release);
    pad.addEventListener('pointercancel', release);
    pads.append(pad);
  });
}

function showStats(): void {
  if (audio) {
    const s = audio.readStats();
    $('stats').textContent = [
      `sample rate     ${s.sampleRate} Hz`,
      `base latency    ${s.baseLatencyMs.toFixed(1)} ms`,
      `output latency  ${s.outputLatencyMs.toFixed(1)} ms (as reported by the browser)`,
      `audio load      ${s.loadPercent.toFixed(2)} %`,
      `voices          ${s.activeVoices}`,
      `transport       ${s.shared ? 'SharedArrayBuffer' : 'postMessage fallback'}`,
    ].join('\n');
  }
  requestAnimationFrame(showStats);
}

$('start').addEventListener('click', async () => {
  $<HTMLButtonElement>('start').disabled = true;
  audio = await JamAudio.start();
  $('start').hidden = true;
  $('play').hidden = false;
  api.setInstrument(Number($<HTMLSelectElement>('instrument').value));
  api.ready = true;
  showStats();
});

$<HTMLSelectElement>('instrument').addEventListener('change', (e) => {
  api.setInstrument(Number((e.target as HTMLSelectElement).value));
});
