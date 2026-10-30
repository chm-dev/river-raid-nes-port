/*
 * nes_steer.mjs - steering ramp ($A455-$A493) against the Atari trace.
 *
 * assets/atari/runtime/play_left: holding Left from rest moves $57 124 -> 123 on
 * the first frame, 122 at +8, 121 at +11, ... 111 at +28 (the speed $2B grows by
 * 8 per frame and the jet moves when the 8-bit fraction $58 borrows).  The port
 * runs the jet every other frame (two 60 Hz ticks), so the displacement is
 * compared at even frame counts, with 1 px slack.  Also checks that a one-frame
 * tap nudges the jet by 1-2 px and that releasing resets the ramp.
 *
 * Run:  node tests/nes_steer.mjs [rom] [noi]
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol, startGame } from './mcp_client.mjs';

const rom = path.resolve(process.argv[2] || 'roms/river_raid.nes');
const noi = path.resolve(process.argv[3] || 'roms/river_raid.noi');
const sym = symbolsFromNoi(noi);
const S = n => requireSymbol(sym, n);
let failed = 0;
const check = (ok, msg, extra = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${msg}  ${extra}`); if (!ok) failed++; };
async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}
const press = b => callTool('mesen_input_override', { action: 'set', port: 0, buttons: b });
const jetX = async () => (await readBytes(S('player'), 1))[0];

// Atari displacement (x - 124) after n frames of Left, from the capture.
const ATARI = { 2: -1, 8: -2, 12: -3, 16: -5, 20: -7, 24: -9, 28: -13 };
const ATARI_DX = { 1: -1, 8: -2, 11: -3, 14: -4, 16: -5, 18: -6, 20: -7, 21: -8, 23: -9, 24: -10, 26: -11, 27: -12, 28: -13 };

await init();
try {
    await callTool('mesen_load_rom', { filepath: rom });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await startGame();
    await step(420);
    await writeBytes(S('player_invuln'), [1]);
    await press('A');
    await step(10);
    await press('');
    await step(6);

    const x0 = await jetX();
    await press('Left');
    const seen = {};
    for (let f = 1; f <= 40; f++) {
        await step(1);
        seen[f] = (await jetX()) - x0;
    }
    await press('');
    console.log('left from rest, dx per frame:', Object.values(seen).join(' '));
    for (const n of [8, 16, 24, 28]) {
        // The port updates every other frame, so allow the Atari value at n-1..n.
        const want = ATARI_DX[n] ?? ATARI[n];
        check(Math.abs(seen[n] - want) <= 1, `after ${n} frames dx ~ ${want}`, `got ${seen[n]}`);
    }
    check(seen[4] >= -2 && seen[4] <= -1, 'the first frames move only 1-2 px', `dx(4) = ${seen[4]}`);

    // Release, then a one-frame tap.
    await step(10);
    const x1 = await jetX();
    await press('Right');
    await step(2);
    await press('');
    await step(6);
    const tap = (await jetX()) - x1;
    check(tap >= 1 && tap <= 2, 'a short tap nudges the jet 1-2 px', `dx = ${tap}`);

    // Reaches full speed (1 px/frame) after the ramp.
    await press('Right');
    await step(40);
    const a = await jetX();
    await step(8);
    const b = await jetX();
    await press('');
    check(b - a === 8, 'full speed is 1 px per frame', `dx over 8 frames = ${b - a}`);
} finally {
    await press('');
}
console.log(failed ? `${failed} FAILED` : 'all passed');
process.exit(failed ? 1 : 0);
