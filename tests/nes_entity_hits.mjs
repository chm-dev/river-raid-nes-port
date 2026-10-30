/*
 * nes_entity_hits.mjs - jet-vs-entity box boundaries (20 cases).
 *
 * Freezes the river, plants one helicopter / ship / fuel depot at offsets around
 * the jet (7 vs 8 px, 13 vs 14 rows, the wide ship, the 24-row fuel depot) and
 * checks that the jet dies (lives drop) or refuels exactly where the 8x14 box
 * overlaps.  Guards entities_check_player() / ent_scan_player().
 *
 * Run:  node tests/nes_entity_hits.mjs
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol, startGame } from './mcp_client.mjs';

const ROM = path.resolve('roms/river_raid.nes');
const SYM = symbolsFromNoi(path.resolve('roms/river_raid.noi'));
const S = n => requireSymbol(SYM, n);

// type, rows, cols, then cases as [dx, dy, expected] relative to the jet (px, py)
const groups = [
    { type: 8, name: 'helicopter 8x10', cases: [
        [0, 0, 'hit'], [7, 0, 'hit'], [-7, 0, 'hit'], [0, 13, 'hit'], [0, -9, 'hit'],
        [8, 0, 'miss'], [-8, 0, 'miss'], [0, 14, 'miss'], [0, -10, 'miss'], [40, 0, 'miss'] ] },
    { type: 13, name: 'ship 16x8', cases: [
        [-15, 0, 'hit'], [7, 0, 'hit'], [0, -7, 'hit'],
        [-16, 0, 'miss'], [8, 0, 'miss'], [0, -8, 'miss'] ] },
    { type: 15, name: 'fuel 8x24', cases: [
        [0, 0, 'fuel'], [0, -23, 'fuel'], [0, -24, 'miss'], [8, 0, 'miss'] ] },
];

async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}

async function runCase(type, dx, dy) {
    await callTool('mesen_load_rom', { filepath: ROM });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await startGame();
    await step(420);
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: 'A' });
    await step(6);
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });
    await step(10);
    await writeBytes(S('river_freeze'), [1]);
    await writeBytes(S('player_invuln'), [0]);
    await step(3);
    const [px, py] = await readBytes(S('player'), 2);
    await writeBytes(S('ent_type'), new Array(11).fill(0));
    await writeBytes(S('hud_lives'), [3]);
    await writeBytes(S('hud_fuel'), [100]);
    const ex = (px + dx) & 255, ey = (py + dy) & 255;
    await writeBytes(S('ent_type') + 3, [type]);
    await writeBytes(S('ent_x') + 3, [ex]);
    await writeBytes(S('ent_y') + 3, [ey]);
    await writeBytes(S('ent_flags') + 3, [0]);
    await writeBytes(S('ent_aux') + 3, [0]);
    await writeBytes(S('entity_count'), [1]);
    await step(4);
    const lives = (await readBytes(S('hud_lives'), 1))[0];
    const fuel = (await readBytes(S('hud_fuel'), 1))[0];
    return { px, py, lives, fuel };
}

await init();
let bad = 0, total = 0;
for (const g of groups) {
    for (const [dx, dy, want] of g.cases) {
        const r = await runCase(g.type, dx, dy);
        const got = r.lives < 3 ? 'hit' : (r.fuel > 100 ? 'fuel' : 'miss');
        const ok = got === want;
        total++; if (!ok) bad++;
        console.log(`${ok ? 'ok  ' : 'FAIL'} ${g.name.padEnd(16)} d=(${String(dx).padStart(3)},${String(dy).padStart(3)}) jet=(${r.px},${r.py}) want ${want.padEnd(4)} got ${got} (lives ${r.lives}, fuel ${r.fuel})`);
    }
}
console.log(`\n${total - bad}/${total} cases pass`);
