/*
 * nes_river_cache.mjs - Phase 4 tile-cache / streaming validation.
 *
 * Steps frame by frame (paused, deterministic) and, after every frame, checks
 * that each completed river tile row in the 30-row nametable ring that is
 * currently on screen references CHR tiles holding exactly the pattern the
 * Atari model produced (tests/data/river_tiles_golden.json).  A mismatching
 * row is fine while it is outside the visible window (pending stripe, stale
 * slot); a mismatching row inside the window is a visible glitch.
 *
 * Needs: python tools/gen_river_tiles_golden.py
 */
import fs from 'node:fs';
import path from 'node:path';

import {
    init,
    callTool,
    readBytes,
    readU8,
    readU16,
    writeBytes,
    symbolsFromNoi,
    requireSymbol,
    sleep,
} from './mcp_client.mjs';

const ROM = path.resolve('roms/river_raid.nes');
const NOI = path.resolve('roms/river_raid.noi');
const GOLDEN = path.resolve('tests/data/river_tiles_golden.json');
const FRAMES = Number(process.env.FRAMES || 800);
const RING = 30;
const FIRST_SLOT = 28;          /* slot of tile row 0 (the init lead row) */
const VISIBLE_LINES = 204;      /* scanlines 36..239 */
const TILE_COLS = 24;           /* whole 48-byte Atari row */
const LEFT_COL = 4;            /* first river nametable column */

let failures = 0;
function check(name, ok, detail = '') {
    console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
    if (!ok) failures++;
}

const hex = a => Array.from(a, v => v.toString(16).padStart(2, '0')).join('');

async function main() {
    const golden = JSON.parse(fs.readFileSync(GOLDEN, 'utf8'));
    const sym = symbolsFromNoi(NOI);
    const distAddr = requireSymbol(sym, 'river_distance');
    const scrollAddr = requireSymbol(sym, 'playfield_scroll_y');

    await init();
    await callTool('mesen_load_rom', { filepath: ROM });
    await sleep(900);
    await callTool('mesen_playback', { action: 'pause' });
    await sleep(100);

    let checkedRows = 0;
    let glitches = 0;
    let firstGlitch = '';
    let outsideMismatch = 0;
    let lastDist = 0;
    let maxDist = 0;

    for (let f = 0; f < FRAMES; f++) {
        await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
        if (f === 2) {
            /* After crt0 has cleared RAM: freeze the jet, un-freeze the river. */
            await writeBytes(requireSymbol(sym, 'player_disable'), [1]);
            await writeBytes(requireSymbol(sym, 'river_freeze'), [0]);
        }
        const dist = await readU16(distAddr);
        const scroll = await readU8(scrollAddr);
        const nt = await readBytes(0x2400, 960, 'NesPpuMemory');
        const chr = await readBytes(0x0000, 4096, 'NesChrRam');
        lastDist = dist;
        maxDist = Math.max(maxDist, dist);
        if (dist >= golden.length) break;

        for (let s = Math.max(0, dist - RING); s < dist; s++) {
            // Tile rows 1 and 2 are the game-start road (recoloured land);
            // the golden stream models the object-free river.
            if (s === 1 || s === 2) continue;
            const slot = (((FIRST_SLOT - s) % RING) + RING) % RING;
            const want = golden[s];
            let ok = true;
            for (let c = 0; c < TILE_COLS && ok; c++) {
                const tile = nt[slot * 32 + LEFT_COL + c];
                const got = hex(chr.slice(tile * 16, tile * 16 + 16));
                ok = got === want.slice(c * 32, c * 32 + 32);
            }
            checkedRows++;
            if (ok) continue;
            /* Ring pixel rows slot*8 .. slot*8+7 vs the visible window. */
            let visible = false;
            for (let l = 0; l < 8 && !visible; l++) {
                const rel = (slot * 8 + l - scroll + 240) % 240;
                visible = rel < VISIBLE_LINES;
            }
            if (visible) {
                glitches++;
                if (!firstGlitch) firstGlitch = `frame ${f} step ${s} slot ${slot} dist ${dist} scroll ${scroll}`;
            } else {
                outsideMismatch++;
            }
        }
    }

    console.log(`frames=${FRAMES} tile rows completed=${maxDist} (last ${lastDist}) rows checked=${checkedRows} ` +
                `hidden mismatches=${outsideMismatch}`);
    check('every on-screen tile row matches the Atari model', glitches === 0,
          glitches ? `${glitches} glitches, first: ${firstGlitch}` : '');
    check('generator ran long enough', maxDist >= Math.floor(FRAMES / 9) - 2, `distance=${maxDist}`);
    console.log(failures ? 'river cache test FAILED' : 'river cache test passed');
    process.exitCode = failures ? 1 : 0;
}

main().catch(err => {
    console.error(err);
    process.exitCode = 1;
});
