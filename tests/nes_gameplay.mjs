/*
 * nes_gameplay.mjs - Phase 5 gameplay checks: steering/banking, throttle, fuel
 * burn, refuelling on a depot and a shot scoring a kill.
 *
 * Entities are planted straight into the slot arrays (the spawner is random), so
 * the test does not depend on what the river generates.  `player_invuln` keeps the
 * jet alive through the steering/throttle/shot parts; it is cleared for the fuel
 * burn, which needs the real fuel counter.
 *
 * Run:  node tests/nes_gameplay.mjs [rom] [noi]
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
// player_state_t: x, y, speed, bank, ...
const jet = async () => { const p = await readBytes(S('player'), 4); return { x: p[0], y: p[1], bank: p[3] }; };
const clearEntities = () => writeBytes(S('ent_type'), new Array(11).fill(0));
const plant = async (slot, type, x, y) => {
    await writeBytes(S('ent_type') + slot, [type]);
    await writeBytes(S('ent_x') + slot, [x]);
    await writeBytes(S('ent_y') + slot, [y]);
    await writeBytes(S('ent_flags') + slot, [0]);
    await writeBytes(S('ent_aux') + slot, [0]);
};

await init();
try {
    await callTool('mesen_load_rom', { filepath: rom });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await step(420);                    // river scrolled in, jet waiting
    await setU8('player_invuln', 1);
    await press('A');
    await step(10);
    await press('');
    await step(2);

    // Steering and banking.
    const x0 = (await jet()).x;
    await press('Left');
    await step(20);
    const l = await jet();
    check(l.x < x0 && l.bank === 1, 'left moves the jet and banks it', `x ${x0} -> ${l.x}, bank ${l.bank}`);
    await press('Right');
    await step(40);
    const r = await jet();
    check(r.x > l.x && r.bank === 2, 'right moves the jet and banks it', `x ${l.x} -> ${r.x}, bank ${r.bank}`);
    await press('');
    await step(4);
    check((await jet()).bank === 0, 'the jet straightens without input');

    // Throttle: down = slow (0), none = cruise (1), up = fast (2).
    const speeds = [];
    for (const b of ['Down', '', 'Up']) {
        await press(b);
        await step(6);
        speeds.push(await u8('river_speed'));
    }
    await press('');
    check(speeds.join() === '0,1,2', 'Down/none/Up give slow/cruise/fast', `river_speed ${speeds.join(',')}`);

    // Refuel: a depot on the jet adds 2 units per frame (original $A40A).
    await step(6);
    await setU8('player_invuln', 0);
    await clearEntities();
    const j = await jet();
    await setU8('hud_fuel', 100);
    await plant(0, 15, j.x - 4, j.y);
    await step(4);
    const fuel = await u8('hud_fuel');
    check(fuel >= 106 && fuel <= 110, 'a fuel depot refuels about 2 units per frame', `hud_fuel 100 -> ${fuel} in 4 frames`);

    // Fuel burn: one unit per 8 frames with nothing in the way.
    await clearEntities();
    await setU8('hud_fuel', 200);
    await step(64);
    const burnt = 200 - (await u8('hud_fuel'));
    check(burnt >= 7 && burnt <= 9, 'fuel burns one unit per 8 frames', `burnt ${burnt} in 64 frames`);

    // Shot scores a kill: a ship (30 points) planted ahead of the jet.
    await setU8('player_invuln', 1);
    await clearEntities();
    const k = await jet();
    await plant(0, 13, k.x - 4, k.y - 90);
    const s0 = await readBytes(S('hud_score_bcd'), 3);
    let kills = false;
    for (let f = 0; f < 120 && !kills; f += 4) {
        await press('A');
        await step(2);
        await press('');
        await step(2);
        kills = (await u8('ent_type')) < 7;
    }
    const s1 = await readBytes(S('hud_score_bcd'), 3);
    check(kills, 'a shot destroys the ship (slot becomes an explosion)');
    check(s1[2] - s0[2] === 0x30 && s1[1] === s0[1], 'the ship is worth 30 points', `score bcd ${[...s0]} -> ${[...s1]}`);
} finally {
    await press('');
}
console.log(failed ? `gameplay test FAILED (${failed})` : 'gameplay test passed');
process.exit(failed ? 1 : 0);
