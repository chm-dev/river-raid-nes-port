/*
 * nes_start_clear.mjs - no enemy or depot is left close to the jet when a game
 * (or a respawn) starts: on every start level the river scrolls in, the jet
 * appears, and everything within ENT_START_CLEARANCE scanlines above it has been
 * removed (the Atari's closest object stops 45+ px short of its jet).
 *
 * Run:  node tests/nes_start_clear.mjs [rom] [noi]
 */
import path from 'node:path';
import { init, callTool, readBytes, sleep, symbolsFromNoi, requireSymbol } from './mcp_client.mjs';

const rom = path.resolve(process.argv[2] || 'roms/river_raid.nes');
const sym = symbolsFromNoi(path.resolve(process.argv[3] || 'roms/river_raid.noi'));
const S = n => requireSymbol(sym, n);
const CLEARANCE = 48;
const ROWS = [0,0,0,0,0,0,0,6,10,10,10,10,20,8,20,24,11,16];      // ent_gfx_rows
let failed = 0;
const check = (ok, msg, extra = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${msg}  ${extra}`); if (!ok) failed++; };
const step = async n => { for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' }); };
const press = b => callTool('mesen_input_override', { action: 'set', port: 0, buttons: b });

await init();
try {
    for (let v = 0; v < 4; v++) {
        await callTool('mesen_load_rom', { filepath: rom });
        await sleep(1500);
        await callTool('mesen_playback', { action: 'pause' });
        await step(60);
        for (let k = 0; k < v; k++) { await press('Up'); await step(6); await press(''); await step(6); }
        await step(20); await press('Start'); await step(8); await press('');
        // Watch the whole scroll-in: no object may ever slide into the zone above the jet
        // (it would be visible there until the jet appears and the safety net removes it).
        const seenInZone = [];
        for (let f = 0; f < 600; f += 2) {
            await step(2);
            const tt = [...await readBytes(S('ent_type'), 11)], yy = [...await readBytes(S('ent_y'), 11)];
            tt.forEach((a, i) => {
                if (a >= 7 && a !== 14 && a < 16 && yy[i] < 240 && yy[i] + ROWS[a] + CLEARANCE > 218) seenInZone.push(`f${f} t${a}@${yy[i]}`);
            });
            if ((await readBytes(S('river_freeze'), 1))[0] === 1) break;
        }
        await step(10);                                   // jet waiting
        const frozen = (await readBytes(S('river_freeze'), 1))[0];
        const t = [...await readBytes(S('ent_type'), 11)];
        const y = [...await readBytes(S('ent_y'), 11)];
        const jet = (await readBytes(S('player'), 2))[1];
        const near = t.map((a, i) => ({ a, i }))
            .filter(o => o.a >= 7 && o.a !== 14 && o.a < 16 && y[o.i] + ROWS[o.a] + CLEARANCE > jet && y[o.i] < 240)
            .map(o => `t${o.a}@y${y[o.i]}`);
        check(frozen === 1 && near.length === 0 && seenInZone.length === 0, `start level ${[1, 5, 20, 50][v]}: nothing within ${CLEARANCE} px above the jet, even while scrolling in`,
            `jet y ${jet}; objects ${t.map((a, i) => a ? `t${a}@${y[i]}` : '').filter(Boolean).join(' ')}${near.length ? ' TOO CLOSE: ' + near.join(' ') : ''}${seenInZone.length ? ' SEEN IN ZONE: ' + seenInZone.slice(0, 4).join(' ') : ''}`);
    }
} finally {
    await press('');
}
console.log(failed ? `\n${failed} FAILED` : '\nall passed');
process.exit(failed ? 1 : 0);
