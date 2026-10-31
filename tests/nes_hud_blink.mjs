/*
 * nes_hud_blink.mjs - the start blink and the white P2 field (Atari $B69A, $B68E).
 *
 * On the Atari the active player's label + score are painted gray for 16 of every
 * 64 frames ($18 & $30 == 0) from the (re)start until the first input ($76 in
 * 1..$7F); the idle field never blinks.  The P2 label and score are white
 * (COLPF1 = $8E), P1's yellow.  Checked here on the live nametable / CHR-RAM:
 *   - READY (1P): the left field (P1 + score) blanks in 16-frame runs, HI never does
 *   - after the first input nothing blanks
 *   - 2P: right field is P2 in white tiles (tiles 1..10 and 58/59 hold plane0 = $FF)
 *   - 1P: the right digits are black again
 *   - after P1 dies, P2 is the active player: the right field blinks, the left never
 *
 * Run:  node tests/nes_hud_blink.mjs [rom] [noi]
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
const press = (b, port = 0) => callTool('mesen_input_override', { action: 'set', port, buttons: b });
async function tap(b, port = 0) { await press(b, port); await step(6); await press('', port); await step(6); }

const ROW1 = 0x2000 + 32;
let topLeak = 0;
// Row 1 of the HUD nametable: P1 label 1..2, score 11..16, right label 20..21, right digits 25..30.
async function fields() {
    const r = await readBytes(ROW1, 32, 'NesPpuMemory');
    const r0 = await readBytes(ROW1 - 32, 32, 'NesPpuMemory');   // the raised digits' top tiles
    // The top row blanks together with the field: it never keeps digit tops on a gray band.
    if (r.slice(11, 17).every(t => t === 0) && r0.slice(11, 17).some(t => t !== 0)) topLeak++;
    if (r.slice(25, 31).every(t => t === 0) && r0.slice(25, 31).some(t => t !== 0)) topLeak++;
    return {
        p1: r.slice(1, 3), score: r.slice(11, 17), label: r.slice(20, 22), right: r.slice(25, 31),
        leftBlank: r.slice(1, 3).every(t => t === 0) && r.slice(11, 17).every(t => t === 0),
        rightBlank: r.slice(20, 22).every(t => t === 0) && r.slice(25, 31).every(t => t === 0),
    };
}
const t = async () => (await readBytes(S('sys_time'), 1))[0];

// Sample `n` frames; returns per-frame {blank, phase} for the chosen side.
async function sample(side, n) {
    const out = [];
    for (let i = 0; i < n; i++) {
        await step(1);
        const f = await fields();
        out.push({ blank: side === 'left' ? f.leftBlank : f.rightBlank, off: (await t() & 0x30) === 0 });
    }
    return out;
}
function runs(samples) {
    const lens = [];
    let cur = 0, started = false;
    for (const s of samples) {
        if (s.blank) { cur++; } else { if (started && cur) lens.push(cur); cur = 0; started = true; }
    }
    return lens;                       // complete blank runs only
}
const plane0 = async tile => await readBytes(0x1000 + tile * 16, 8, 'NesChrRam');
const allFF = a => a.every(b => b === 0xFF);

await init();
try {
    await callTool('mesen_load_rom', { filepath: rom });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await step(120);

    // ---- 1P: attract has no blink, READY does ------------------------------
    let f = await fields();
    check(!f.leftBlank && f.p1[0] === 27 && f.label[0] === 29, 'attract: P1 and HI labels are visible', JSON.stringify(f));
    check(!allFF(await plane0(1)), 'one player: right digits are black (tile 1 plane 0 is not solid)', '');
    check(allFF(await plane0(58)), 'the P2 label tile is white (plane 0 solid)', '');

    await tap('Start');
    await step(420);                    // river scrolls in, jet shown, waiting for input
    let s = await sample('left', 192);
    const nBlank = s.filter(x => x.blank).length;
    const rl = runs(s);
    check(nBlank >= 40 && nBlank <= 56, `READY: left field blanks ~16 of 64 frames (${nBlank}/192)`, '');
    check(rl.length >= 2 && rl.every(l => l >= 14 && l <= 18), 'blank runs are 16 frames long (+-2)', JSON.stringify(rl));
    const agree = s.filter(x => x.blank === x.off).length;
    check(agree >= s.length * 0.85, `blank phase follows sys_time & $30 == 0 (${agree}/${s.length} frames agree)`, '');
    s = await sample('right', 64);
    check(s.every(x => !x.blank), 'READY: the idle HI field never blinks', '');

    // first input ends the blink and restores the field
    await press('A');
    await step(12);
    await press('');
    await step(8);
    s = await sample('left', 80);
    check(s.every(x => !x.blank), 'after the first input the score no longer blinks', '');
    f = await fields();
    check(f.p1[0] === 27 && f.p1[1] === 28, 'P1 label restored', JSON.stringify(f.p1));

    // ---- 2P: white P2 field -------------------------------------------------
    await setU8('hud_lives', 1);        // last life: die -> game over -> attract
    await setU8('hud_fuel', 0);
    await step(900);
    check((await u8('hud_lives')) === 0, 'back in attract', '');
    await tap('Up');                    // GAME 2 = level 1, two players
    await step(80);
    f = await fields();
    check(f.label[0] === 58 && f.label[1] === 59, 'two players: right label is P2', JSON.stringify(f.label));
    let ok = true;
    for (let d = 1; d <= 10; d++) ok = ok && allFF(await plane0(d));
    check(ok, 'two players: right-field digit tiles 1..10 are white', '');
    ok = true;
    for (let k = 252; k <= 255; k++) ok = ok && allFF(await plane0(k));
    check(ok, 'two players: the right-field top tiles 252..255 are white', '');
    const gd = await readBytes(0x1000 + 47 * 16, 16, 'NesChrRam');
    check(!allFF(gd.filter((_, i) => i % 2 === 0)), 'GAME digit has its own black tile (47)', JSON.stringify(gd.slice(0, 4)));
    const gt = await readBytes(0x2000 + 3 * 32 + 5, 1, 'NesPpuMemory');
    check(gt[0] === 47, 'GAME digit uses tile 47', JSON.stringify(gt));
    check(f.right.every(t => t === 0 || (t >= 1 && t <= 10)), 'P2 score uses the white digit tiles', JSON.stringify(f.right));

    await tap('Down');                  // GAME 1 = one player again
    await step(80);
    ok = true;
    for (let d = 1; d <= 10; d++) ok = ok && !allFF(await plane0(d));
    f = await fields();
    check(ok && f.label[0] === 29, 'one player again: black digits and HI label', JSON.stringify(f.label));
    await tap('Up');                    // two players

    // ---- 2P turn swap: P2 is active, so the right field blinks --------------
    await tap('Start');
    await step(420);
    s = await sample('left', 70);
    check(s.some(x => x.blank), 'P1 READY (2P): the left field blinks', '');
    await press('A'); await step(12); await press(''); await step(8);
    await setU8('hud_fuel', 0);         // P1 dies
    await step(420);
    check((await u8('hud_cur')) === 1, 'P2 is the active player', '');
    s = await sample('right', 192);
    const nr = s.filter(x => x.blank).length;
    check(nr >= 40 && nr <= 56, `P2 READY: the right field blinks (${nr}/192 frames)`, '');
    s = await sample('left', 64);
    check(s.every(x => !x.blank), 'P2 READY: the idle P1 field stays visible', '');
    await press('A', 1); await step(12); await press('', 1); await step(8);
    s = await sample('right', 80);
    check(s.every(x => !x.blank), 'after P2\'s first input the blink stops', '');
    check(topLeak === 0, 'a blanked field never leaves digit top tiles on row 0', `${topLeak} frames`);
} finally {
    await press('');
    await press('', 1);
}
console.log(failed ? `\n${failed} FAILED` : '\nALL PASSED');
process.exit(failed ? 1 : 0);
