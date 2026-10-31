/*
 * nes_jet_hitbox.mjs - the jet collides with its lit pixels, like the Atari's
 * P2PF test, not with its 8-px cell, and the 4-px bank outline (playfield colour
 * 2) is harmless: only the land colour beyond it counts (P2PF bit 0).
 *
 * The straight jet (shape 9) lights columns 1..7, so on a frozen river with the
 * water spanning [lo, hi) under all 14 rows it survives at px = lo-5 and
 * px = hi-4 and crashes one pixel further out on either side.  (The ring stores
 * one bound pair per 8-row tile row; lo/hi are the tightest over the rows the
 * jet touches.)
 *
 * Run:  node tests/nes_jet_hitbox.mjs [rom] [noi]
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

async function setup() {
    await callTool('mesen_load_rom', { filepath: rom });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await startGame();
    await step(420);
    await press('A');
    await step(10);
    await press('');
    await step(2);
    await writeBytes(S('river_freeze'), [1]);
    await step(4);
    const p = await readBytes(S('player'), 2);
    const y = p[1];
    const sc = (await readBytes(S('playfield_scroll_y'), 1))[0];
    const L = await readBytes(S('river_left_x'), 30);
    const R = await readBytes(S('river_right_x'), 30);
    let lo = 0, hi = 255;
    for (let r = 0; r < 14; r++) {
        const slot = (((sc + y + r - 30) % 240) >> 3);
        lo = Math.max(lo, L[slot]); hi = Math.min(hi, R[slot]);
    }
    return { lo, hi, y };
}
const alive = async () => (await readBytes(S('player') + 7, 1))[0];

await init();
try {
    const trials = [
        ['lo-5 (straight jet touches the first land pixel, past the 4-px outline)', g => g.lo - 5, true],
        ['hi-4 (right wing flush with the first right land pixel)', g => g.hi - 4, true],
        ['lo-6 (one pixel into the left land)', g => g.lo - 6, false],
        ['hi-3 (one pixel into the right land)', g => g.hi - 3, false],
    ];
    for (const [name, px, survives] of trials) {
        const g = await setup();
        await writeBytes(S('player'), [32 + px(g)]);
        await step(4);
        const a = await alive();
        check((a !== 0) === survives, `${name}`, `water [${g.lo}, ${g.hi}), alive=${a}`);
    }
} finally {
    await press('');
}
console.log(failed ? `${failed} FAILED` : 'all passed');
process.exit(failed ? 1 : 0);
