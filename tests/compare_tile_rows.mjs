/*
 * compare_tile_rows.mjs - capture the on-screen river tile pixels per generated
 * row (resolved through the nametable and CHR-RAM) so two ROMs can be diffed.
 * The content must be identical whatever the timing; used for the tile-job
 * dedupe and the repeat-row skip.
 *
 * Run:  node tests/compare_tile_rows.mjs <out.json> [frames]   (per ROM, then diff the JSON)
 */
import path from 'node:path';
import fs from 'node:fs';
import { init, callTool, readBytes, readU16, writeBytes, symbolsFromNoi, requireSymbol, sleep, startGame } from './mcp_client.mjs';

const out = process.argv[2];
const FRAMES = Number(process.argv[3] || 800);
const sym = symbolsFromNoi(path.resolve('roms/river_raid.noi'));
const S = n => requireSymbol(sym, n);
const RING = 30, FIRST_SLOT = 28, LEFT = 4, COLS = 24, LAG = 20;

await init();
await callTool('mesen_load_rom', { filepath: path.resolve('roms/river_raid.nes') });
await sleep(900);
await callTool('mesen_playback', { action: 'pause' });
await startGame();
await sleep(100);

const rows = {};
let lastDist = -1;
for (let f = 0; f < FRAMES; f++) {
    await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
    if (f === 2) {
        await writeBytes(S('player_disable'), [1]);
        await writeBytes(S('river_freeze'), [0]);
    }
    const dist = await readU16(S('river_distance'));
    if (dist === lastDist) continue;
    lastDist = dist;
    const s = dist - LAG;
    if (s < 0 || rows[s]) continue;
    const slot = (((FIRST_SLOT - s) % RING) + RING) % RING;
    const nt = await readBytes(0x2400 + slot * 32 + LEFT, COLS, 'NesPpuMemory');
    const chr = await readBytes(0x0000, 4096, 'NesChrRam');
    rows[s] = nt.map(t => chr.slice(t * 16, t * 16 + 16).map(b => b.toString(16).padStart(2, '0')).join('')).join('');
}
fs.writeFileSync(out, JSON.stringify(rows));
console.log(`captured ${Object.keys(rows).length} rows -> ${out}`);
