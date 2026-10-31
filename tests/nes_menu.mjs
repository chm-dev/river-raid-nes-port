/*
 * nes_menu.mjs - attract mode, start-level selection and the two-player turn
 * swap.
 *
 * Boot goes straight to the attract screen (no jet, no lives).  Up/Down step the
 * Atari game number 1..8 (GAME n = 1 + 2*variant + two: start level 1/5/20/50 and
 * one/two players together), START begins, SELECT begins a debug game in which the
 * jet cannot crash.  The scoreboard's right label is HI (one player) or P2 (two).
 * In a two-player game every death hands the jet to the other player, who keeps
 * their own lives.  During play START pauses (RESUME / EXIT menu).
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
// variant = the start level picked in the menu, (GAME - 1) >> 1; river_variant is the
// level whose river is on screen and only follows once the idle timer has run out.
const menu = async () => {
    const game = await u8('hud_game');
    return {
        game, lives: await u8('hud_lives'),
        bridge: await u8('hud_bridge'), variant: (game - 1) >> 1,
    };
};

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

    // Up/Down step the Atari game number 1..8 ($A69D): bit 0 of (game - 1) is
    // "two players", bits 1-2 the start level.
    await tap('Up');
    m = await menu();
    check(m.game === 2 && m.variant === 0 && m.bridge === 1 && (await u8('hud_two')) === 1,
        'Up: GAME 2 = level 1, two players', JSON.stringify(m));
    await tap('Up');
    m = await menu();
    check(m.game === 3 && m.variant === 1 && m.bridge === 5 && (await u8('hud_two')) === 0,
        'Up: GAME 3 = level 5, one player', JSON.stringify(m));
    for (let i = 0; i < 5; i++) await tap('Up');
    m = await menu();
    check(m.game === 8 && m.variant === 3 && m.bridge === 50 && (await u8('hud_two')) === 1,
        'Up x5: GAME 8 = level 50, two players', JSON.stringify(m));
    await tap('Up');
    m = await menu();
    check(m.game === 1 && m.variant === 0 && m.bridge === 1, 'Up wraps from GAME 8 to GAME 1', JSON.stringify(m));
    await tap('Down');
    m = await menu();
    check(m.game === 8 && m.variant === 3, 'Down wraps from GAME 1 to GAME 8', JSON.stringify(m));
    for (let i = 0; i < 4; i++) await tap('Down');   // GAME 4 = level 5, two players
    m = await menu();
    check(m.game === 4 && m.variant === 1 && m.bridge === 5, 'level 5, two players: GAME 4', JSON.stringify(m));

    // Atari ($A6D8, $A2F5, $A688): changing the game freezes the old river and its
    // enemies; the picked level's river begins from black only after the idle timer
    // ($2A = $25, one tick per 8 frames = 296 frames) runs out, and every change
    // reloads the timer.
    const frozenNow = async () => ({
        rv: await u8('river_variant'), freeze: await u8('river_freeze'),
        hold: await u8('ent_hold'), y: await u8('playfield_scroll_y'),
    });
    let fz = await frozenNow();
    await step(90);
    let fz2 = await frozenNow();
    check(fz.rv === 0 && fz.freeze === 1 && fz.hold === 1 && fz2.y === fz.y && fz2.rv === 0,
        'game changed: the old river and its enemies stand still', JSON.stringify([fz, fz2]));
    await tap('Up');
    await tap('Down');                  // net no change, but both presses reload the timer
    await step(230);
    fz2 = await frozenNow();
    check(fz2.rv === 0 && fz2.freeze === 1 && fz2.y === fz.y,
        'another change reloads the idle timer (still frozen 350 frames after the first change)', JSON.stringify(fz2));
    await step(100);
    fz2 = await frozenNow();
    check(fz2.rv === 1 && fz2.freeze === 0 && fz2.hold === 0,
        'idle timer out: the picked level (5) is built and scrolls', JSON.stringify(fz2));
    await step(20);
    check((await u8('playfield_scroll_y')) !== fz2.y, 'the new river scrolls', '');

    // Scoreboard: the right-hand label is P2 ($4F glyph tiles 58/59) for two
    // players and HI (29/30) for one.
    await step(60);
    let lab = await readBytes(0x2000 + 1 * 32 + 20, 2, 'NesPpuMemory');
    check(lab[0] === 58 && lab[1] === 59, 'two players: the right label is P2', JSON.stringify(lab));
    await tap('Down');                   // GAME 3 = one player
    await step(60);
    lab = await readBytes(0x2000 + 1 * 32 + 20, 2, 'NesPpuMemory');
    check(lab[0] === 29 && lab[1] === 30, 'one player: the right label is HI', JSON.stringify(lab));
    await tap('Up');                     // GAME 4 again
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
    check((await u8('hud_cur')) === 0, 'hud_cur follows the active player', '');

    // ---- SELECT debug start and the pause menu (fresh one-player game) ----
    const oamY = async slot => (await readBytes(S('shadow_OAM') + slot * 4, 1))[0];
    const startOnePlayer = async button => {
        await callTool('mesen_load_rom', { filepath: rom });
        await sleep(1500);
        await callTool('mesen_playback', { action: 'pause' });
        await step(120);
        await tap(button);
        await step(420);                          // the river scrolls in, the jet waits
        await press('A'); await step(10); await press(''); await step(4);
    };

    // Control: a normal START crashes into the bank within a few seconds.
    await startOnePlayer('Start');
    await setU8('player_invuln', 0);
    await press('Left'); await step(360); await press('');
    m = await menu();
    check(m.lives < 3, 'START game: flying into the bank costs a life', JSON.stringify(m));

    // SELECT: the same game, but the jet cannot crash into terrain.
    await startOnePlayer('Select');
    await setU8('player_invuln', 0);
    await press('Left'); await step(360); await press('');
    m = await menu();
    check(m.lives === 3, 'SELECT game: the bank does not hurt (still 3 lives)', JSON.stringify(m));

    // Pause: START freezes the world and shows RESUME / EXIT.
    await startOnePlayer('Start');
    await setU8('player_invuln', 1);
    const scrollY = () => u8('playfield_scroll_y');
    await step(20);
    await tap('Start');
    await step(10);
    const frozen = await scrollY();
    await step(30);
    check((await scrollY()) === frozen && (await u8('river_freeze')) === 1, 'START pauses: the river stops', `scroll ${frozen}`);
    check((await oamY(56)) !== 240 && (await oamY(2)) !== 240, 'pause menu sprites are shown', '');
    await tap('Start');                           // RESUME is selected first
    await step(10);
    check((await u8('river_freeze')) === 0 && (await oamY(56)) === 240, 'RESUME continues and hides the menu', '');
    await step(10);
    check((await scrollY()) !== frozen, 'the river scrolls again after RESUME', '');
    await tap('Start');                           // pause again
    await tap('Down');                            // cursor on EXIT
    await tap('Start');
    await step(30);
    m = await menu();
    check(m.lives === 0 && (await oamY(56)) === 240, 'EXIT returns to the attract screen (no lives, menu hidden)', JSON.stringify(m));
} finally {
    await press('');
}
console.log(failed ? `\n${failed} FAILED` : '\nall passed');
process.exit(failed ? 1 : 0);
