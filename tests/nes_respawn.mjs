/*
 * nes_respawn.mjs - losing a life restarts the river and the jet waits for input.
 *
 * Flies into the left bank, then checks that the death costs a life, the river
 * restarts at the level-1 checkpoint (rg_level == 1, the ring is refilled and
 * scrolls back in), the jet waits stopped on the river, and play resumes on input.
 * The checkpoint choice itself ($B1C0 rule) needs a bridge and is covered by
 * reading river.c against the disassembly, not by this test.
 *
 * Run:  node tests/nes_respawn.mjs [rom] [noi]
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol, startGame } from './mcp_client.mjs';

const rom = path.resolve(process.argv[2] || 'roms/river_raid.nes');
const noi = path.resolve(process.argv[3] || 'roms/river_raid.noi');
const sym = symbolsFromNoi(noi);
const S = n => requireSymbol(sym, n);
let failed = 0;
const check = (ok, msg, extra = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${msg}  ${extra}`); if (!ok) failed++; };
const u8 = async n => (await readBytes(S(n), 1))[0];
async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}
const press = b => callTool('mesen_input_override', { action: 'set', port: 0, buttons: b });

await init();
try {
    await callTool('mesen_load_rom', { filepath: rom });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await startGame();
    await step(420);                    // river scrolled in, jet waiting
    const lives0 = await u8('hud_lives');
    await press('A');
    await step(6);
    await press('Left');
    let crashed = false;
    for (let f = 0; f < 900 && !crashed; f += 5) {
        await step(5);
        crashed = (await u8('hud_lives')) < lives0;
    }
    await press('');
    check(crashed, 'flying into the bank costs a life', `lives ${lives0} -> ${await u8('hud_lives')}`);

    await step(60);
    check((await u8('river_freeze')) === 1, 'the river stays frozen during the death animation');
    await step(450);                    // death delay, restart, river scrolls back in (as at game start)
    const level = await u8('rg_level');
    check(level === 1, 'the checkpoint is level 1', `rg_level=${level}`);
    check((await u8('river_freeze')) === 1, 'the jet waits on the river for input');

    const ring = await readBytes(S('river_right_x'), 30);
    // the two hidden rows (above and below the window) may still be empty
    check(ring.filter(v => v !== 0).length >= 28, 'the collision ring is refilled', ring.join(' '));

    const before = (await readBytes(S('playfield_scroll_y'), 1))[0];
    await step(10);
    check((await readBytes(S('playfield_scroll_y'), 1))[0] === before, 'the scroll is stopped while waiting');
    await press('A');
    await step(30);
    await press('');
    check((await u8('river_freeze')) === 0, 'input restarts the river');
    check((await readBytes(S('playfield_scroll_y'), 1))[0] !== before, 'the river scrolls again');
    check((await u8('hud_lives')) === lives0 - 1, 'only one life was lost');
} finally {
    await press('');
}
console.log(failed ? `respawn test FAILED (${failed})` : 'respawn test passed');
process.exit(failed ? 1 : 0);
