/*
 * perf_scroll_cadence.mjs - how the river scroll actually moves.
 *
 * Steps frames while paused (invulnerable jet, shooting every 20 frames) for
 * slow (Down), cruise and fast (Up) flight and prints, per scenario: frames per
 * main-loop pass (2 = an overrun), rows scrolled per frame, the average speed,
 * and how often the generator was blocked by a pending tile job.  Cruise should
 * read 1.00 px/frame with almost no overruns.  Symbols come from the .noi.
 *
 * Run:  node tests/perf_scroll_cadence.mjs     (MesenCE-MCP already running)
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol, startGame } from './mcp_client.mjs';

const ROM = path.resolve('roms/river_raid.nes');
const BASE = 0x300, LEN = 0x500;
const SYM = symbolsFromNoi(path.resolve('roms/river_raid.noi'));
const S = n => requireSymbol(SYM, n);
const A = { game_ticks: S('game_ticks'), vel: S('river_vel'), ents: S('entity_count'), scroll: S('playfield_scroll_y'), frame: S('frame_counter'), owed: S('river_owed'), job: S('river_job'), scrolled: S('river_scrolled') };
const u8 = (m, a) => m[a - BASE];
const u16 = (m, a) => m[a - BASE] | (m[a - BASE + 1] << 8);

async function frames(n, rec) {
    for (let i = 0; i < n; i++) {
        await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
        if (rec) {
            const m = await readBytes(BASE, LEN);
            rec.push({ pass: u16(m, A.frame), scroll: u8(m, A.scroll), owed: u8(m, A.owed),
                       job: u8(m, A.job), ents: u8(m, A.ents), ticks: u8(m, A.game_ticks),
                       vel: u8(m, A.vel), scrolled: u8(m, A.scrolled) });
        }
    }
}

async function scenario(name, buttons, n) {
    await callTool('mesen_load_rom', { filepath: ROM });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await startGame();
    await writeBytes(S('player_invuln'), [1]);                 // player_invuln
    await frames(420, null);                      // river fills, jet READY
    await writeBytes(S('player_invuln'), [1]);
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: 'A' });
    await frames(6, null);
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: buttons });
    await frames(240, null);                      // settle the throttle ramp
    const rec = [];
    // keep firing now and then: toggle A every 20 frames for shot load
    for (let k = 0; k < n / 20; k++) {
        const b = (k % 2 ? 'A,' : '') + buttons;
        await callTool('mesen_input_override', { action: 'set', port: 0, buttons: b.replace(/,$/, '') });
        await frames(20, rec);
    }
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });
    return rec;
}

function analyse(name, rec) {
    // main-loop passes: frames where `pass` changed
    const passes = [];
    let lastPass = rec[0].pass, lastIdx = 0;
    for (let i = 1; i < rec.length; i++) {
        if (rec[i].pass !== lastPass) {
            passes.push({ idx: i, gap: i - lastIdx, dpass: (rec[i].pass - lastPass) & 0xFFFF, row: rec[i] });
            lastPass = rec[i].pass; lastIdx = i;
        }
    }
    const gaps = {}; passes.forEach(p => gaps[p.gap] = (gaps[p.gap] || 0) + 1);
    // scroll per pass (scroll decrements, mod 240)
    let rows = {}, prev = rec[0].scroll, total = 0, zero = 0, owedCap = 0, jobPend = 0;
    const perFrame = {};
    for (let i = 1; i < rec.length; i++) {
        const d = (prev - rec[i].scroll + 240) % 240; prev = rec[i].scroll; total += d;
        perFrame[d] = (perFrame[d] || 0) + 1;
        if (rec[i].owed >= 4) owedCap++;
        if (rec[i].job) jobPend++;
    }
    const avgEnt = rec.reduce((s, r) => s + r.ents, 0) / rec.length;
    console.log(`\n== ${name}: ${rec.length} frames, ${passes.length} passes`);
    console.log(' frames per pass (gap:count):', JSON.stringify(gaps));
    console.log(' scroll rows per frame (rows:count):', JSON.stringify(perFrame));
    console.log(` avg px/frame ${(total / (rec.length - 1)).toFixed(2)}  frames owed>=4: ${owedCap}  frames with tile job pending: ${jobPend}  avg entities ${avgEnt.toFixed(1)}`);
    console.log(' ticks values:', JSON.stringify([...new Set(rec.map(r => r.ticks))]), ' vel:', JSON.stringify([...new Set(rec.map(r => r.vel))]));
}

await init();
const out = {};
for (const [name, btn] of [['slow(Down)', 'Down'], ['cruise', ''], ['fast(Up)', 'Up']]) {
    const rec = await scenario(name, btn, 600);
    analyse(name, rec);
}
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });
