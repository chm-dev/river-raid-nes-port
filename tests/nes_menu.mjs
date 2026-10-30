/*
 * nes_menu.mjs - attract mode, start-level selection and the two-player turn
 * swap.
 *
 * Boot goes straight to the attract screen (no jet, no lives).  Up/Down step the
 * start level 1/5/20/50 (river_variant 0..3), SELECT toggles one/two players
 * (GAME n = 1 + 2*variant + two), START begins.  In a two-player game every
 * death hands the jet to the other player, who keeps their own lives.
 *
 * Run:  node tests/nes_menu.mjs [rom] [noi]
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol } from './mcp_client.mjs';

const rom = path.resolve(process.argv[2] || 'roms/river_raid.nes');
const noi = path.resolve(process.argv[3] || 'roms/river_raid.noi');
const sym = symbolsFromNoi(noi);
const S = n => requireSymbol(sym, n);
let failed = 0;
const check = (ok, msg, extra = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${msg}  ${extra}`); if (!ok) failed++; };
const u8 = async n => (await readBytes(S(n), 1))[0];
const setU8 = (n, v) => writeBytes(S(n), [v]);
async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}
const press = b => callTool('mesen_input_override', { action: 'set', port: 0, buttons: b });
async function tap(b) {
    await press(b);
    await step(6);
    await press('');
    await step(6);
}
const menu = async () => ({
    game: await u8('hud_game'), lives: await u8('hud_lives'),
    bridge: await u8('hud_bridge'), variant: await u8('river_variant'),
});

await init();
try {
    await callTool('mesen_load_rom', { filepath: rom });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await step(120);

    let m = await menu();
    check(m.game === 1 && m.lives === 0 && m.bridge === 1 && m.variant === 0,
        'boots into attract: GAME 1, no lives, bridge 1', JSON.stringify(m));
    const alive = async () => (await readBytes(S('player'), 8))[6];   // unused byte: just proves reads work
    void alive;

    await tap('Up');
    m = await menu();
    check(m.variant === 1 && m.game === 3 && m.bridge === 5, 'Up: start level 5, GAME 3', JSON.stringify(m));
    await tap('Up');
    await tap('Up');
    m = await menu();
    check(m.variant === 3 && m.game === 7 && m.bridge === 50, 'Up twice more: start level 50, GAME 7', JSON.stringify(m));
    await tap('Up');
    m = await menu();
    check(m.variant === 0 && m.game === 1 && m.bridge === 1, 'Up wraps to start level 1', JSON.stringify(m));
    await tap('Down');
    m = await menu();
    check(m.variant === 3 && m.bridge === 50, 'Down wraps to start level 50', JSON.stringify(m));
    await tap('Select');
    m = await menu();
    check(m.game === 8, 'SELECT toggles two players: GAME 8', JSON.stringify(m));
    await tap('Select');
    m = await menu();
    check(m.game === 7, 'SELECT again: back to GAME 7', JSON.stringify(m));
    await step(60);

    // Start a two-player game on level 5: Down x2 from 50 -> 20 -> 5.
    await tap('Down'); await tap('Down');
    await tap('Select');
    m = await menu();
    check(m.variant === 1 && m.game === 4 && m.bridge === 5, 'level 5, two players: GAME 4', JSON.stringify(m));
    await tap('Start');
    await step(420);
    m = await menu();
    check(m.lives === 3 && m.bridge === 5, 'START begins the game with 3 lives on bridge 5', JSON.stringify(m));
    await press('A');
    await step(10);
    await press('');
    await step(4);

    // Player 1 dies (out of fuel): player 2 takes over with their own 3 lives.
    await setU8('hud_fuel', 0);
    await step(8);
    m = await menu();
    check(m.lives === 2, 'player 1 loses a life (3 -> 2)', JSON.stringify(m));
    await step(420);
    m = await menu();
    check(m.lives === 3, 'player 2 takes over with their own 3 lives', JSON.stringify(m));
    // Player 2 flies on the second controller (as on the Atari's port 2).
    const press2 = b => callTool('mesen_input_override', { action: 'set', port: 1, buttons: b });
    await press('A');
    await step(10);
    await press('');
    await step(4);
    check(m.lives === 3 && (await u8('river_freeze')) === 1, 'player 2 waits: port 1 has not been touched', '');
    await press2('A');
    await step(10);
    await press2('');
    await step(4);
    await setU8('hud_fuel', 0);
    await step(8);
    m = await menu();
    check(m.lives === 2, 'player 2 loses a life (3 -> 2)', JSON.stringify(m));
    await step(420);
    m = await menu();
    check(m.lives === 2, 'player 1 is back with their remaining 2 lives', JSON.stringify(m));
} finally {
    await press('');
}
console.log(failed ? `\n${failed} FAILED` : '\nall passed');
process.exit(failed ? 1 : 0);
