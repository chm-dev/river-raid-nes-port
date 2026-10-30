/*
 * nes_sprite_lock.mjs - do entity sprites stay locked to the scrolling river?
 *
 * Plants a fuel depot, lets the river scroll and, every frame, checks that
 * OAM Y + playfield_scroll_y is constant.  An error of 1 px on alternate frames
 * is the 30 Hz-entities jitter that entities_follow() removes.  Fast flight can
 * show a few 1-2 px errors on passes that overrun (an NMI landing mid-pass).
 *
 * Run:  node tests/nes_sprite_lock.mjs [rom] [noi] [Up|Down]
 *       (defaults: roms/river_raid.nes and its .noi, cruise)
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol } from './mcp_client.mjs';

const rom = path.resolve(process.argv[2] || 'roms/river_raid.nes');
const noi = path.resolve(process.argv[3] || 'roms/river_raid.noi');
const buttons = process.argv[4] || '';
const sym = symbolsFromNoi(noi);
const S = n => requireSymbol(sym, n);

async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}
await init();
await callTool('mesen_load_rom', { filepath: rom });
await sleep(1500);
await callTool('mesen_playback', { action: 'pause' });
await writeBytes(S('player_invuln'), [1]);
await step(420);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: 'A' });
await step(6);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons });
await step(120);                       // let the throttle settle and the entities spawn
// a fuel depot never patrols; plant it high on the screen on the right bank side of the jet
await writeBytes(S('ent_type'), new Array(11).fill(0));
await writeBytes(S('ent_type') + 0, [15]);
await writeBytes(S('ent_x') + 0, [150]);
await writeBytes(S('ent_y') + 0, [50]);
await writeBytes(S('ent_flags') + 0, [0]);
await writeBytes(S('ent_aux') + 0, [0]);
await writeBytes(S('entity_count'), [1]);
await step(4);

const ks = [];
for (let f = 0; f < 80; f++) {
    await step(1);
    const scroll = (await readBytes(S('playfield_scroll_y'), 1))[0];
    // find the depot's first sprite: lowest-numbered used entity slot (slot 6 on)
    const oam = await readBytes(0x0200 + 24, 4);
    ks.push({ scroll, y: oam[0], tile: oam[1] });
}
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });
const keyOf = r => (r.y + r.scroll) % 240;
const counts = {};
ks.forEach(r => { const k = keyOf(r); counts[k] = (counts[k] || 0) + 1; });
const modal = Object.entries(counts).sort((a, b) => b[1] - a[1])[0];
const off = ks.filter(r => keyOf(r) !== Number(modal[0])).length;
// differences between consecutive frames' sprite Y vs scroll delta
let steps = {};
for (let i = 1; i < ks.length; i++) {
    const dy = (ks[i].y - ks[i - 1].y + 256) % 256;
    const ds = (ks[i - 1].scroll - ks[i].scroll + 240) % 240;
    const e = (dy - ds + 256) % 256;
    const k = e > 128 ? e - 256 : e;
    steps[k] = (steps[k] || 0) + 1;
}
console.log(`${buttons || 'cruise'}: ${ks.length} frames, sprite locked to the river on ${ks.length - off}; off by ${off}. per-frame lag error (px:frames) ${JSON.stringify(steps)}`);
