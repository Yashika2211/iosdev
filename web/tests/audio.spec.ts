import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';

// The native build records this hash; every browser must reproduce it.
const goldenHeader = readFileSync(new URL('../../engine/tools/golden.h', import.meta.url), 'utf8');
const GOLDEN = goldenHeader.match(/kGoldenHash48k = 0x([0-9a-fA-F]+)ULL/)![1].toLowerCase();

type Stats = { tick: number; activeVoices: number; peak: number; blocks: number; shared: boolean; lateEvents: number; droppedEvents: number };

async function start(page: Page): Promise<void> {
  await page.goto('/');
  await page.click('#start');
  await page.waitForFunction(() => (window as any).jam?.ready === true, null, { timeout: 30_000 });
}

const stats = (page: Page) => page.evaluate(() => (window as any).jam.audio.readStats() as Stats);

test('page is cross-origin isolated, so events use SharedArrayBuffer', async ({ page }) => {
  await start(page);
  expect(await page.evaluate(() => crossOriginIsolated)).toBe(true);
  expect((await stats(page)).shared).toBe(true);
});

test('worklet renders continuously and a note makes sound', async ({ page }) => {
  await start(page);
  const before = await stats(page);
  await page.waitForTimeout(500);
  const running = await stats(page);
  expect(running.blocks).toBeGreaterThan(before.blocks);
  expect(running.tick).toBeGreaterThan(before.tick);

  await page.evaluate(() => (window as any).jam.noteOn(64, 120));
  await page.waitForTimeout(300);
  const playing = await stats(page);
  expect(playing.activeVoices).toBeGreaterThan(0);
  expect(playing.peak).toBeGreaterThan(0.01);
  expect(playing.droppedEvents).toBe(0);
});

test('drums play through the same path', async ({ page }) => {
  await start(page);
  await page.evaluate(() => (window as any).jam.setInstrument(2));
  await page.waitForTimeout(100);
  await page.evaluate(() => (window as any).jam.noteOn(0, 127));
  await page.waitForTimeout(150);
  expect((await stats(page)).peak).toBeGreaterThan(0.01);
});

test('wasm inside the AudioWorklet matches the native golden hash', async ({ page }) => {
  await start(page);
  const hash = await page.evaluate(() => (window as any).jam.audio.golden(48000, 128) as Promise<string>);
  expect(hash.padStart(16, '0')).toBe(GOLDEN);
});

test('tapping a pad plays a note', async ({ page }) => {
  await start(page);
  const pad = page.locator('.pad').first();
  const box = (await pad.boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.waitForTimeout(200);
  expect((await stats(page)).activeVoices).toBeGreaterThan(0);
  await page.mouse.up();
});
