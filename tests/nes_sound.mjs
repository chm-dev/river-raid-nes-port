/*
 * nes_sound.mjs - Phase 6 APU checks.  A persistent Lua write callback on
 * $4000-$4017 records, per register, how often it was written and the last value
 * (mailbox in CHR-RAM $1F00: counts at +0, last values at +32).  The test then
 * drives the game and checks that each event produces the expected writes: the
 * shot chirp, the engine pitch per throttle step, a kill's explosion, the refuel
 * tone, the low-fuel warning and the silence afterwards.
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
const u8 = async n => (await readBytes(S(n), 1))[0];
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

const lua = `
local base = 0x1F00
for i = 0, 63 do emu.write(base + i, 0, emu.memType.nesChrRam) end
emu.addMemoryCallback(function(address, value)
  local i = address - 0x4000
  local c = emu.read(base + i, emu.memType.nesChrRam)
  emu.write(base + i, (c + 1) & 255, emu.memType.nesChrRam)
  emu.write(base + 32 + i, value, emu.memType.nesChrRam)
end, emu.callbackType.write, 0x4000, 0x4017)
emu.log("apu recorder armed")
`;

// reg(0x4000 + i): { n: write count, v: last value }
const snap = async () => {
    const m = await readBytes(0x1F00, 64, 'NesChrRam');
    return r => ({ n: m[r - 0x4000], v: m[32 + r - 0x4000] });
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

    let s = await snap();
    check(s(0x4015).v === 0x0F, 'all four channels are enabled', `$4015 = ${s(0x4015).v.toString(16)}`);
    check(s(0x4017).v === 0x40, 'frame IRQ is inhibited', `$4017 = ${s(0x4017).v.toString(16)}`);
    check(s(0x4008).v === 0, 'the engine is silent before the game starts', `$4008 = ${s(0x4008).v.toString(16)}`);

    // Start with a shot: pulse 1 chirps, then falls silent.
    const hi0 = s(0x4003).n;
    await press('A');
    await step(4);
    await press('');
    s = await snap();
    check(s(0x4003).n > hi0, 'firing starts the pulse 1 chirp', `$4003 writes ${hi0} -> ${s(0x4003).n}`);
    check((s(0x4000).v & 0x0F) !== 0, 'the chirp is audible', `$4000 = ${s(0x4000).v.toString(16)}`);
    await step(30);
    s = await snap();
    check(s(0x4000).v === 0x30, 'the chirp ends in silence', `$4000 = ${s(0x4000).v.toString(16)}`);

    // Engine pitch follows the throttle: slow / cruise / fast.
    const pitch = [];
    for (const b of ['Down', '', 'Up']) {
        await press(b);
        await step(8);
        s = await snap();
        pitch.push(s(0x400A).v.toString(16));
    }
    await press('');
    check(s(0x4008).v === 0x81, 'the engine triangle is running', `$4008 = ${s(0x4008).v.toString(16)}`);
    check(pitch.join() === '0,40,c0', 'engine pitch per throttle step', `$400A ${pitch.join(',')}`);

    // A kill: the noise channel fires, then goes silent.
    await clearEntities();
    const k = await jet();
    await plant(0, 13, k.x - 4, k.y - 90);
    const nz0 = s(0x400F).n;
    for (let f = 0; f < 120 && (await u8('ent_type')) >= 7; f += 4) {
        await press('A');
        await step(2);
        await press('');
        await step(2);
    }
    s = await snap();
    check(s(0x400F).n > nz0, 'a kill starts the noise explosion', `$400F writes ${nz0} -> ${s(0x400F).n}`);
    await step(60);
    s = await snap();
    check(s(0x400C).v === 0x30, 'the explosion ends in silence', `$400C = ${s(0x400C).v.toString(16)}`);

    // Refuelling: a depot under the jet plays the pulse 2 tone.
    await clearEntities();
    await setU8('player_invuln', 0);
    await setU8('hud_fuel', 120);
    const j = await jet();
    await plant(0, 15, j.x - 4, j.y);
    await step(6);
    s = await snap();
    check(s(0x4004).v === 0x76, 'refuelling plays the pulse 2 tone', `$4004 = ${s(0x4004).v.toString(16)}`);
    await clearEntities();
    await step(10);
    s = await snap();
    check(s(0x4004).v === 0x30, 'the tone stops when the depot is left', `$4004 = ${s(0x4004).v.toString(16)}`);

    // Low fuel: pulse 2 beeps on and off.
    await setU8('hud_fuel', 30);
    const seen = new Set();
    for (let f = 0; f < 40; f += 2) {
        await step(2);
        await setU8('hud_fuel', 30);
        seen.add((await snap())(0x4004).v);
    }
    check(seen.has(0xB8) && seen.has(0x30), 'low fuel beeps pulse 2 on and off', `$4004 seen ${[...seen].map(v => v.toString(16))}`);
} finally {
    await press('');
}
console.log(failed ? `${failed} FAILED` : 'sound test passed');
process.exit(failed ? 1 : 0);
