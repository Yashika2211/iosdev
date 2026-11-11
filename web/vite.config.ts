import { defineConfig } from 'vite';

// Cross-origin isolation enables SharedArrayBuffer, which lets the UI hand
// events to the audio thread without postMessage allocating there.
const isolation = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
};

export default defineConfig({
  server: { headers: isolation },
  preview: { headers: isolation },
  build: { target: 'es2022' },
});
