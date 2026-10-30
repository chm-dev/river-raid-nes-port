/*
 * nes_sound.mjs - Phase 6 APU checks against the Atari's captured register
 * stream (tools/atari_sound_trace.py, tools/atari_sound_events.py).
 *
 * A persistent Lua end-frame callback copies the live 2A03 state into CHR-RAM
 * $1F80 (volume, real period and length counter per channel), so every check
 * below is on what the console would actually emit - a channel with volume 0
 * or a zero length counter is silent.
 *
 * What the original plays and this test locks in:
 *   shot       pulse 1, 50% duty, volume 8, period 256 -> 550 (405 -> 196 Hz)
 *              in 15 one-frame steps, then volume 0
 *   engine     noise 17-bit poly, volume 6/4/3 and period 11/12/13 for
 *              fast/cruise/slow (AUDC1 = $B4AC[$19 & 3])
 *   explosion  noise, 23 frames (object destroyed) / 55 frames (jet destroyed)
 *   fuel tank  pulse 2, 50% duty, constant 499 Hz, volume 8 -> 1, one 8-frame
 *              note repeated back-to-back while the jet is over the tank
 *              ($A40A / $B2E8; $7F = 9 is the shot-vs-helicopter sound, not this)
 *   triangle   never used
 *
 * Run:  node tests/nes_sound.mjs [rom] [noi]
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol } from './mcp_client.mjs';

const rom = path.resolve(process.argv[2] || 'roms/river_raid.nes');
const noi = path.resolve(process.argv[3] || 'roms/river_raid.noi');
const sym = symbolsFromNoi(noi);
const S = n => requireSymbol(sym, n);
let failed = 0;
const check = (ok, msg, extra = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${msg}  ${extra}`); if (!ok) failed++; };
const setU8 = (n, v) => writeBytes(S(n), [v]);
async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}
const press = b => callTool('mesen_input_override', { action: 'set', port: 0, buttons: b });
const clearEntities = () => writeBytes(S('ent_type'), new Array(11).fill(0));
const plant = async (slot, type, x, y) => {
    await writeBytes(S('ent_type') + slot, [type]);
    await writeBytes(S('ent_x') + slot, [x]);
    await writeBytes(S('ent_y') + slot, [y]);
    await writeBytes(S('ent_flags') + slot, [0]);
    await writeBytes(S('ent_aux') + slot, [0]);
};
const jet = async () => { const p = await readBytes(S('player'), 4); return { x: p[0], y: p[1] }; };

// 0 s1 vol, 1-2 s1 realPeriod, 3 s2 vol, 4-5 s2 realPeriod, 6 noise vol,
// 7-8 noise period (CPU cycles per LFSR step), 9 s1 length, 10 noise length,
// 11 triangle length+volume (0 = never used).
const lua = `
emu.addEventCallback(function()
  local s = emu.getState()
  local ct = emu.memType.nesChrRam
  local b = 0x1F80
  emu.write(b + 0, s['apu.square1.envelope.volume'], ct)
  local p = s['apu.square1.realPeriod']
  emu.write(b + 1, p & 255, ct)
  emu.write(b + 2, (p >> 8) & 255, ct)
  emu.write(b + 3, s['apu.square2.envelope.volume'], ct)
  local q = s['apu.square2.realPeriod']
  emu.write(b + 4, q & 255, ct)
  emu.write(b + 5, (q >> 8) & 255, ct)
  emu.write(b + 6, s['apu.noise.envelope.volume'], ct)
  local r = s['apu.noise.timer.period']
  emu.write(b + 7, r & 255, ct)
  emu.write(b + 8, (r >> 8) & 255, ct)
  emu.write(b + 9, s['apu.square1.envelope.lengthCounter.counter'], ct)
  emu.write(b + 10, s['apu.noise.envelope.lengthCounter.counter'], ct)
  emu.write(b + 11, (s['apu.triangle.envelope.volume'] or 0) + (s['apu.triangle.linearCounter.counter'] or 0), ct)
end, emu.eventType.endFrame)
emu.log("apu sampler armed")
`;

const apu = async () => {
    const m = await readBytes(0x1F80, 12, 'NesChrRam');
    return {
        s1vol: m[0], s1per: m[1] | (m[2] << 8),
        s2vol: m[3], s2per: m[4] | (m[5] << 8),
        nvol: m[6], nper: m[7] | (m[8] << 8),
        s1len: m[9], nlen: m[10], tri: m[11]
    };
};

await init();
try {
    await callTool('mesen_load_rom', { filepath: rom });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await step(5);
    await callTool('mesen_run_lua_script', { code: lua, waitMs: 500, persistent: true });
    await step(415);                    // river scrolled in, jet waiting
    await setU8('player_invuln', 1);

    let a = await apu();
    check(a.s1vol === 0 && a.s2vol === 0 && a.nvol === 0, 'silent before the game starts',
        `s1=${a.s1vol} s2=${a.s2vol} noise=${a.nvol}`);
    check(a.tri === 0, 'the triangle is never used', `triangle ${a.tri}`);

    // ---- shot: pulse 1, 405 -> 196 Hz in 15 steps, volume 8 --------------
    await clearEntities();
    await press('A');
    a = await apu();
    for (let i = 0; i < 8 && a.s1vol !== 8; i++) {
        await step(1);
        a = await apu();
    }
    await press('');
    check(a.s1vol === 8, 'the shot is a volume-8 pulse', `volume ${a.s1vol}`);
    check(a.s1len > 0, 'the shot channel is audible (length counter loaded)', `length ${a.s1len}`);
    const sweep = [a.s1per];
    for (let i = 0; i < 15; i++) {
        await step(1);
        sweep.push((await apu()).s1per);
    }
    // Keep the rising run (a second shot can start once this one is spent).
    const run = [sweep[0]];
    for (let i = 1; i < sweep.length && sweep[i] > run[run.length - 1]; i++) run.push(sweep[i]);
    const first = run[0], last = run[run.length - 1];
    check(first >= 254 && first <= 258, 'the shot starts at 405 Hz (period 256)', `first ${first} (${Math.round(1789773 / (16 * first))} Hz)`);
    check(last >= 546 && last <= 554, 'the shot falls to 196 Hz (period 550)', `last ${last} (${Math.round(1789773 / (16 * last))} Hz)`);
    check(run.length >= 13, 'the shot sweep has 15 one-frame steps', `${run.length} steps: ${run.join(',')}`);
    await step(6);
    a = await apu();
    check(a.s1vol === 0, 'the shot ends in silence', `volume ${a.s1vol}`);

    // ---- engine: noise, volume and pitch per throttle --------------------
    await clearEntities();
    await step(40);                     // let any explosion finish
    const eng = [];
    for (const b of ['Down', '', 'Up']) {
        await press(b);
        await step(24);                 // the pitch glides (8 frames per step)
        a = await apu();
        eng.push([a.nvol, a.nper]);
    }
    await press('');
    check(eng[0][0] === 3 && eng[1][0] === 4 && eng[2][0] === 6,
        'the engine volume follows the throttle (3/4/6)', JSON.stringify(eng));
    check(a.nlen > 0, 'the engine noise is audible (length counter loaded)', `length ${a.nlen}`);
    check(eng[0][1] > eng[1][1] && eng[1][1] > eng[2][1],
        'the engine hiss pitch follows the throttle', `noise periods ${eng.map(e => e[1]).join(' > ')}`);
    check(a.s1vol === 0 && a.s2vol === 0, 'the engine is noise, not a pulse',
        `s1=${a.s1vol} s2=${a.s2vol} noise=${a.nvol}`);

    // The pitch must slide, not jump: slow -> fast takes several frames.
    await press('Down');
    await step(24);
    await press('Up');
    let early = 0, mid = 0;
    for (let i = 0; i < 6; i++) { await step(1); early += (await apu()).nper; }
    for (let i = 0; i < 20; i++) { await step(1); mid = (await apu()).nper; }
    await press('');
    check(early / 6 < 1016 && early / 6 > 760 && mid === 507,
        'the engine pitch glides from slow to fast over several frames',
        `first 6-frame mean ${Math.round(early / 6)}, settled ${mid}`);

    // ---- explosion: noise, then the engine comes back --------------------
    await clearEntities();
    await press('A');
    await step(2);
    await press('');
    await step(30);
    let j = await jet();
    await plant(0, 13, j.x - 4, j.y - 90);
    for (let f = 0; f < 120 && (await readBytes(S('ent_type'), 1))[0] >= 7; f += 4) {
        await press('A');
        await step(2);
        await press('');
        await step(2);
    }
    await step(2);
    a = await apu();
    check(a.nvol >= 13, 'a kill starts a loud noise explosion', `volume ${a.nvol}`);
    await press('');
    await step(30);
    a = await apu();
    check(a.nvol >= 3 && a.nvol <= 6, 'the engine resumes when the explosion ends', `volume ${a.nvol}`);

    // ---- fuel tank: one 499 Hz note repeated while the jet is over it ----
    await clearEntities();
    await setU8('player_invuln', 0);
    await setU8('hud_fuel', 100);
    j = await jet();
    await plant(0, 15, j.x - 4, j.y);
    await step(2);
    a = await apu();
    const tank = [];
    for (let i = 0; i < 18; i++) {
        await plant(0, 15, j.x - 4, j.y);       // keep the tank under the jet
        await step(1);
        a = await apu();
        tank.push([a.s2vol, a.s2per]);
    }
    const tankVols = tank.map(t => t[0]);
    const tankPers = tank.map(t => t[1]).filter(p => p !== 0);
    check(tankPers.every(p => p === 223),
        'a partly full tank plays 499 Hz', `periods ${[...new Set(tankPers)]} (${Math.round(1789773 / (16 * 224))} Hz)`);
    check(tankVols.includes(8) && tankVols.includes(1),
        'the note volume walks 8 down to 1', `volumes ${tankVols.join(',')}`);
    // 8-frame notes back to back: the volume must reset to 8 repeatedly.
    let restarts = 0;
    for (let i = 1; i < tankVols.length; i++) if (tankVols[i] === 8 && tankVols[i - 1] === 1) restarts++;
    check(restarts >= 1, 'the note repeats while the jet is over the tank',
        `${restarts} restarts: ${tankVols.join(',')}`);
    check(a.nvol >= 3 && a.nvol <= 6,
        'the tank note leaves the noise channel to the engine', `noise volume ${a.nvol}`);
    // Regression: the note used to hang at volume 1 because the hold flag
    // outlived its last frame; it must fall silent within one note (8 frames).
    await clearEntities();
    let lastOn = -1;
    for (let i = 0; i < 16; i++) {
        await step(1);
        if ((await apu()).s2vol > 0) lastOn = i;
    }
    a = await apu();
    check(lastOn <= 9 && a.s2vol === 0,
        'the tank note stops when the tank is passed', `last audible frame ${lastOn}, now ${a.s2vol}`);

    // ---- a full gauge plays the same note an octave higher --------------
    // The Atari's fuel increment only carries when $5B is already $FF, and the
    // carry selects $7A = 3 -> AUDF1 = $1F (999 Hz) instead of $3F (499 Hz).
    await setU8('hud_fuel', 255);
    j = await jet();
    const full = [];
    for (let i = 0; i < 10; i++) {
        await plant(0, 15, j.x - 4, j.y);
        await setU8('hud_fuel', 255);
        await step(1);
        a = await apu();
        full.push([a.s2vol, a.s2per]);
    }
    const fullPers = full.slice(2).map(t => t[1]).filter(p => p !== 0);
    check(fullPers.length >= 6 && fullPers.every(p => p === 111),
        'a full gauge plays the note an octave up (999 Hz)',
        `periods ${[...new Set(fullPers)]} (${Math.round(1789773 / (16 * 112))} Hz)`);
    check(full.map(t => t[0]).includes(8),
        'the full-gauge note keeps the 8 -> 1 volume walk', `volumes ${full.map(t => t[0]).join(',')}`);
    await clearEntities();

    // ---- low-fuel warning: 35-frame rising tone, 28-frame engine gap ------
    // $B2FD: below $40 of fuel, ch1 plays a volume-15 tone at AUDF1 = 2*$7B
    // while $7B ($3E down to $00) is >= $1C, then the engine returns.
    await setU8('player_invuln', 1);
    const warn = [];
    for (let i = 0; i < 70; i++) {
        await setU8('hud_fuel', 30);
        await step(1);
        a = await apu();
        warn.push([a.s1vol, a.s1per, a.nvol]);
    }
    const tone = warn.filter(w => w[0] > 0);
    const gap = warn.filter(w => w[0] === 0);
    // Measure whole runs: the 70-frame window straddles the 63-frame cycle, so
    // the first and last runs may be clipped.
    const runs = [];
    let r0 = [warn[0][0] > 0, 1];
    for (let i = 1; i < warn.length; i++) {
        const on = warn[i][0] > 0;
        if (on === r0[0]) r0[1]++;
        else { runs.push(r0); r0 = [on, 1]; }
    }
    runs.push(r0);
    const toneRuns = runs.filter(r => r[0]).map(r => r[1]);
    const gapRuns = runs.filter(r => !r[0]).map(r => r[1]);
    check(toneRuns.some(n => n >= 33 && n <= 37), 'low fuel plays a 35-frame tone',
        `tone runs ${toneRuns.join(',')}`);
    check(tone.every(w => w[0] === 15), 'the warning tone is at full volume',
        `volumes ${[...new Set(tone.map(w => w[0]))]}`);
    check(gapRuns.some(n => n >= 26 && n <= 30), 'the tone leaves a gap for the engine',
        `gap runs ${gapRuns.join(',')}`);
    // 437 -> 199 means 255 Hz -> 560 Hz: AUDF1 = 2*$7B rising as $7B counts down.
    const pMin = Math.min(...tone.map(w => w[1]));
    check(tone[0][1] === 437 && pMin === 199, 'the warning rises 255 Hz -> 560 Hz',
        `periods ${tone[0][1]} -> ${pMin}`);
    check(tone.every(w => w[2] === 0), 'the warning silences the engine (same POKEY channel)',
        `noise volumes ${[...new Set(tone.map(w => w[2]))]}`);
    check(gap.every(w => w[2] >= 3 && w[2] <= 6), 'the engine returns in the gap',
        `noise volumes ${[...new Set(gap.map(w => w[2]))]}`);
    // A healthy tank must not warn at all.
    await setU8('hud_fuel', 200);
    let warned = 0;
    for (let i = 0; i < 20; i++) {
        await setU8('hud_fuel', 200);
        await step(1);
        if ((await apu()).s1vol !== 0) warned++;
    }
    check(warned === 0, 'a nearly full gauge does not warn', `${warned}/20 frames`);

    // The warning outranks everything: a shot must not cut it short.  Sync to
    // the start of a tone, then fire and require the tone to keep its volume.
    await setU8('hud_fuel', 30);
    for (let g = 0; g < 200 && (await apu()).s1vol !== 0; g++) { await setU8('hud_fuel', 30); await step(1); }
    for (let g = 0; g < 200 && (await apu()).s1vol === 0; g++) { await setU8('hud_fuel', 30); await step(1); }
    await press('A');
    let toneFrames = 0, stolen = 0;
    for (let i = 0; i < 12; i++) {
        await setU8('hud_fuel', 30);
        await step(1);
        const s = await apu();
        if (s.s1vol === 15) toneFrames++;
        else if (s.s1vol !== 0) stolen++;           // the shot would write volume 8
    }
    await press('');
    check(toneFrames >= 11 && stolen === 0, 'a shot does not interrupt the warning tone',
        `${toneFrames} tone frames, ${stolen} dimmed by the shot`);

    // ---- enemy shell: the $B4C0 chirp on pulse 2, then a noise tail ------
    // ent_shell_init() is where the Atari starts $50, so park a tank and a
    // shell one pass away from refiring; ent_shell_tick() will fire it.
    await clearEntities();
    await setU8('player_invuln', 1);
    await setU8('hud_fuel', 200);
    j = await jet();
    await plant(0, 16, j.x, 90);        // ENT_AUX_TANK, driving right
    await plant(1, 17, j.x, 120);       // ENT_AUX_SHELL
    await writeBytes(S('ent_aux'), [4, 56]);
    await writeBytes(S('entity_count'), [2]);
    let shell = null;
    for (let i = 0; i < 12; i++) {
        await step(1);
        const s = await apu();
        if (s.s2vol === 12) { shell = s; break; }
    }
    check(shell !== null, 'the tank firing starts the shell chirp', shell ? `volume ${shell.s2vol}` : 'no chirp');
    const chirp = [];
    for (let i = 0; i < 20; i++) {
        const s = await apu();
        chirp.push([s.s2vol, s.s2per, s.nvol]);
        await step(1);
    }
    const chirpPers = chirp.filter(c => c[0] === 12 && c[1] > 0).map(c => c[1]);
    check(chirpPers.length >= 10 && chirpPers[0] < chirpPers[chirpPers.length - 1],
        'the shell chirp dives in pitch', `periods ${chirpPers.join(',')}`);
    check(chirpPers[0] >= 50 && chirpPers[0] <= 54,
        'the shell chirp starts at 2131 Hz', `first period ${chirpPers[0]}`);
    const tail = chirp.filter(c => c[2] === 12 && c[0] === 0);
    check(tail.length >= 2, 'the swelling fireball adds noise', `${tail.length} noise frames at volume 12`);
    await clearEntities();
} finally {
    await press('');
}
console.log(failed ? `${failed} FAILED` : 'sound test passed');
process.exit(failed ? 1 : 0);
