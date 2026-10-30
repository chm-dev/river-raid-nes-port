/*
 * nes_ring_integrity.mjs - is a river row ever rewritten while it is visible?
 *
 * The playfield is a 30-row (240 px) ring; the window below the HUD shows
 * 240 - split scanlines of it and the generator writes new rows into the part
 * that is hidden.  Shrinking the HUD (bigger window) shrinks that margin, and the
 * 250-slot CHR cache can be overrun at high levels.  Either shows up as a
 * visible ring row whose tiles change between two frames.  This test needs no
 * model: it steps frames, resolves every ring row to its 24 tile patterns
 * (nametable + CHR-RAM) and reports any row that was visible on the previous
 * frame and changed now.
 *
 * Run:  node tests/nes_ring_integrity.mjs [rom] [noi] [split] [Up|Down|none] [frames]
 *       (defaults: roms/river_raid.nes, its .noi, 30, Up, 700)
 * Use split 36 for ROMs built before the compact HUD.
 */
import path from 'node:path';

import {
    init,
    callTool,
    readBytes,
    writeBytes,
    symbolsFromNoi,
    requireSymbol,
    sleep, startGame } from './mcp_client.mjs';

const rom = path.resolve(process.argv[2] || 'roms/river_raid.nes');
const noi = path.resolve(process.argv[3] || 'roms/river_raid.noi');
const split = Number(process.argv[4] || 30);
const buttons = (process.argv[5] || 'Up') === 'none' ? '' : (process.argv[5] || 'Up');
const FRAMES = Number(process.argv[6] || 700);
const RING = 30;
const LEFT_COL = 4;
const COLS = 24;
const WINDOW = 240 - split;
const SETTLE = 320;                 /* frames to skip: the world starts black */

const sym = symbolsFromNoi(noi);
const S = n => requireSymbol(sym, n);

async function step(n) {
    for (let i = 0; i < n; i++) {
        await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
    }
}

function visibleSlots(scroll) {
    const out = [];
    for (let k = 0; k < RING; k++) {
        let vis = false;
        for (let l = 0; l < 8 && !vis; l++) {
            vis = ((8 * k + l - scroll + 240) % 240) < WINDOW;
        }
        if (vis) out.push(k);
    }
    return out;
}

await init();
await callTool('mesen_load_rom', { filepath: rom });
await sleep(1500);
await callTool('mesen_playback', { action: 'pause' });
await startGame();
await writeBytes(S('player_invuln'), [1]);
await step(420);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: 'A' });
await step(6);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons });
await step(SETTLE - 120);

let prev = null;                    /* { scroll, vis:Set, sig:[...] } of the previous frame */
let checked = 0;
let glitches = 0;
const first = [];
for (let f = 0; f < FRAMES; f++) {
    await step(1);
    const scroll = (await readBytes(S('playfield_scroll_y'), 1))[0];
    const nt = await readBytes(0x2400, 960, 'NesPpuMemory');
    const chr = await readBytes(0x0000, 4096, 'NesChrRam');
    const sig = [];
    for (let k = 0; k < RING; k++) {
        let s = '';
        for (let c = 0; c < COLS; c++) {
            const t = nt[k * 32 + LEFT_COL + c];
            s += t.toString(16).padStart(2, '0') + ':' + chr.slice(t * 16, t * 16 + 16).join(',') + ';';
        }
        sig.push(s);
    }
    const vis = new Set(visibleSlots(scroll));
    if (prev && f > 0) {
        for (const k of vis) {
            if (!prev.vis.has(k)) continue;
            checked++;
            if (prev.sig[k] !== sig[k]) {
                glitches++;
                if (first.length < 6) first.push(`frame ${f} slot ${k} scroll ${prev.scroll}->${scroll}`);
            }
        }
    }
    prev = { scroll, vis, sig };
}
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });

console.log(`window ${WINDOW} scanlines (split ${split}), ${buttons || 'cruise'}, ${FRAMES} frames: ` +
            `${checked} visible-row checks, ${glitches} rows changed while visible`);
if (first.length) console.log('first: ' + first.join(' | '));
console.log(glitches ? 'ring integrity test FAILED' : 'ring integrity test passed');
process.exitCode = glitches ? 1 : 0;
