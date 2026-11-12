// Not part of the normal test run: `npm run bench` measures wasm CPU per voice
// in each browser and writes the results to measurements/.
import { test } from '@playwright/test';
import { execSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import os from 'node:os';

test('@bench wasm CPU per voice', async ({ page, browserName, browser }) => {
  test.setTimeout(600_000);
  await page.goto('/');
  const result = await page.evaluate(() => (window as any).jam.runBench('/jam.wasm', 10, 5));
  const out = {
    what: 'Engine CPU cost per voice as wasm on the browser main thread: extra render time of N sustained voices over an idle engine, best of 5 runs',
    measured_on: {
      date: new Date().toISOString().slice(0, 10),
      cpu: os.cpus()[0]?.model,
      os: `${os.type()} ${os.release()}`,
      browser: `${process.env.PW_CHROMIUM_CHANNEL && browserName === 'chromium' ? `${process.env.PW_CHROMIUM_CHANNEL} (installed)` : browserName} ${browser.version()}, headless, via Playwright`,
      power: execSync(`pmset -g batt | head -1 | sed -E "s/.*'(.*)'.*/\\1/"`).toString().trim(),
      low_power_mode: execSync("pmset -g | awk '/lowpowermode/ {print $2}'").toString().trim() === '1',
      engine_commit: execSync('git rev-parse --short HEAD').toString().trim(),
    },
    command: 'cd web && npm run bench',
    sample_rate: 48000,
    block: 128,
    seconds: 10,
    repeats: 5,
    idle_percent_of_core: result.idlePercent,
    results: result.results,
  };
  writeFileSync(new URL(`../../measurements/wasm-cpu-${browserName}.json`, import.meta.url), JSON.stringify(out, null, 2) + '\n');
  console.log(browserName, JSON.stringify(result));
});
