/*
 * compare_oam.mjs - dump the entity sprites of 9 planted scenes (every object
 * type, both facings, clipped at the top/bottom, the 38-sprite cap, an
 * explosion mix) so two ROMs can be diffed byte for byte.  Used to prove the
 * hand-written ent_draw_reg() matches the C draw.  Rotor tiles may differ by
 * one animation frame between ROMs of different speed.
 *
 * Run:  node tests/compare_oam.mjs <rom.nes> <rom.noi> <out.json>   (per ROM, then diff the JSON)
 */
import path from 'node:path';
import fs from 'node:fs';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol, startGame } from './mcp_client.mjs';

const rom = path.resolve(process.argv[2]), noi = path.resolve(process.argv[3]), out = process.argv[4];
const sym = symbolsFromNoi(noi);
const S = n => requireSymbol(sym, n);
const LEFT = 1;

// scenes: list of [slot, type, x, y, flags]
const scenes = {
    heli_right: [[0, 8, 60, 100, 0], [1, 9, 90, 110, 0], [2, 10, 120, 120, 0], [3, 11, 150, 130, 0], [4, 12, 180, 140, 0]],
    heli_left: [[0, 8, 60, 100, LEFT], [1, 9, 90, 110, LEFT], [2, 10, 120, 120, LEFT], [3, 11, 150, 130, LEFT], [4, 12, 180, 140, LEFT]],
    mixed_right: [[0, 7, 50, 90, 0], [1, 13, 80, 100, 0], [2, 15, 140, 90, 0], [3, 14, 60, 140, 0]],
    mixed_left: [[0, 7, 50, 90, LEFT], [1, 13, 80, 100, LEFT], [2, 15, 140, 90, LEFT], [3, 14, 60, 140, LEFT]],
    clip_top: [[0, 8, 60, 20, 0], [1, 13, 100, 30, LEFT], [2, 15, 140, 14, 0], [3, 9, 170, 33, LEFT], [4, 14, 60, 8, 0]],
    clip_bottom: [[0, 8, 60, 225, 0], [1, 13, 100, 229, LEFT], [2, 15, 140, 215, 0], [3, 14, 40, 220, LEFT]],
    cap: [[0, 14, 40, 60, 0], ...Array.from({ length: 10 }, (_, i) => [i + 1, 15, 20 + i * 20, 60 + (i % 3) * 25, i & 1 ? LEFT : 0])],
    explosion_mix: [[0, 8, 60, 100, 0], [1, 3, 100, 110, 8 << 4], [2, 13, 130, 120, LEFT], [3, 5, 170, 130, 13 << 4], [4, 9, 200, 140, 0]],
    one_each: Array.from({ length: 9 }, (_, i) => [i, 7 + i, 30 + i * 22, 70 + (i % 4) * 30, i % 2 ? LEFT : 0]).concat([[9, 8, 120, 180, 0]]),
};

async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
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
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });
await step(10);
await writeBytes(S('river_freeze'), [1]);
await step(4);

const result = {};
for (const [name, ents] of Object.entries(scenes)) {
    await writeBytes(S('ent_type'), new Array(11).fill(0));
    for (const [slot, type, x, y, flags] of ents) {
        await writeBytes(S('ent_type') + slot, [type]);
        await writeBytes(S('ent_x') + slot, [x]);
        await writeBytes(S('ent_y') + slot, [y]);
        await writeBytes(S('ent_flags') + slot, [flags]);
        await writeBytes(S('ent_aux') + slot, [0]);
    }
    await writeBytes(S('entity_count'), [ents.length]);
    await step(8);
    result[name] = await readBytes(0x0200, 176);
}
fs.writeFileSync(out, JSON.stringify(result));
console.log(`wrote ${Object.keys(result).length} scenes -> ${out}`);
