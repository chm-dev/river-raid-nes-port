/*
 * nes_hold.mjs - the world stands still until the player starts: patrolling
 * entities do not move while a new game's river scrolls in (WAIT) or while the
 * jet waits on the river (READY); they move once the player acts.
 *
 * Run:  node tests/nes_hold.mjs [rom] [noi]
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol, startGame } from './mcp_client.mjs';

const rom = path.resolve(process.argv[2] || 'roms/river_raid.nes');
const sym = symbolsFromNoi(path.resolve(process.argv[3] || 'roms/river_raid.noi'));
const S = n => requireSymbol(sym, n);
let failed = 0;
const check = (ok, msg, extra = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${msg}  ${extra}`); if (!ok) failed++; };
const u8 = async n => (await readBytes(S(n), 1))[0];
async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}
const press = b => callTool('mesen_input_override', { action: 'set', port: 0, buttons: b });
// Plant a ship (type 7) well inside the river; ent_x[0] is its patrol position.
const plant = async () => {
    await writeBytes(S('ent_type'), new Array(11).fill(0));
    await writeBytes(S('ent_type') + 0, [7]);
    await writeBytes(S('ent_x') + 0, [120]);
    await writeBytes(S('ent_y') + 0, [100]);
    await writeBytes(S('ent_flags') + 0, [0x02]);   // ENT_F_AWAKE
    await writeBytes(S('ent_aux') + 0, [0]);
    await writeBytes(S('entity_count'), [1]);
};
const x0 = async () => (await readBytes(S('ent_x'), 1))[0];

await init();
try {
    await callTool('mesen_load_rom', { filepath: rom });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    // Level 1 is static in the original; start on bridge 5 (GAME 3: Up twice,
    // GAME 2 is level 1 for two players) so ships patrol.
    await step(30);
    for (let i = 0; i < 2; i++) {
        await press('Up');
        await step(8);
        await press('');
        await step(8);
    }
    await startGame();
    await step(30);                         // river scrolling in (WAIT)
    check((await u8('ent_hold')) === 1, 'ent_hold is set while the river scrolls in');
    await plant();
    const a = await x0();
    await step(40);
    check((await x0()) === a, 'a patrolling ship does not move while the river scrolls in', `x ${a} -> ${await x0()}`);
    // Negative control: with the hold forced off the same ship does move.
    await writeBytes(S('ent_hold'), [0]);
    await plant();
    const n0 = await x0();
    await step(40);
    check((await x0()) !== n0, 'control: the ship moves when the hold is off', `x ${n0} -> ${await x0()}`);
    await writeBytes(S('ent_hold'), [1]);
    await step(400);                        // READY: jet on the river, waiting
    await plant();
    const b = await x0();
    await step(40);
    check((await u8('ent_hold')) === 1 && (await x0()) === b, 'nor while the jet waits for input', `x ${b} -> ${await x0()}`);
    await press('A');
    await step(10);
    await press('');
    check((await u8('ent_hold')) === 0, 'the world is released when the player acts');
} finally {
    await press('');
}
console.log(failed ? `\n${failed} FAILED` : '\nall passed');
process.exit(failed ? 1 : 0);
