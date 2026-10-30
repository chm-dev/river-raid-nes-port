/*
 * cadence_levels.mjs - what the player sees: displayed scroll step per frame and
 * the frames where the sprites and the scroll disagree, by start level and speed.
 *
 * Usage: node cadence_levels.mjs <rom> <noi> <variant 0..3> <''|Up|Down> <frames>
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol } from './mcp_client.mjs';

const [rom, noi, variantArg, btn, framesArg] = process.argv.slice(2);
const SYM = symbolsFromNoi(path.resolve(noi));
const S = n => requireSymbol(SYM, n);
const variant = Number(variantArg || 0), buttons = btn || '', N = Number(framesArg || 600);
const step = async n => { for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' }); };
const press = async (b, n = 4) => { await callTool('mesen_input_override', { action: 'set', port: 0, buttons: b }); await step(n); await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' }); await step(4); };

await init();
await callTool('mesen_load_rom', { filepath: path.resolve(rom) });
await sleep(1500);
await callTool('mesen_playback', { action: 'pause' });
await step(60);
// GAME n = 1 + 2*variant + two: Up steps the game number by one
for (let i = 0; i < variant * 2; i++) await press('Up');
await press('Start', 8);
await writeBytes(S('player_invuln'), [1]);
await step(420);
await writeBytes(S('player_invuln'), [1]);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: 'A' });
await step(6);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons });
await step(240);

const rec = [];
for (let k = 0; k < N / 20; k++) {
    const b = [k % 2 ? 'A' : '', buttons].filter(Boolean).join(',');
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: b });
    for (let i = 0; i < 20; i++) {
        await step(1);
        const sc = (await readBytes(S('playfield_scroll_y'), 1))[0];
        const fc = await readBytes(S('frame_counter'), 2);
        const ents = (await readBytes(S('entity_count'), 1))[0];
        const lvl = (await readBytes(S('rg_level'), 1))[0];
        const owed = (await readBytes(S('river_owed'), 1))[0];
        const job = (await readBytes(S('river_job'), 1))[0];
        rec.push({ sc, fc: fc[0] | (fc[1] << 8), ents, lvl, owed, job });
    }
}
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });

const hist = {}; let late = 0, stalls = 0, maxOwed = 0, jobFrames = 0;
const seq = [];
for (let i = 1; i < rec.length; i++) {
    const d = (rec[i - 1].sc - rec[i].sc + 240) % 240;
    hist[d] = (hist[d] || 0) + 1; seq.push(d);
    if (rec[i].fc === rec[i - 1].fc) late++;
    maxOwed = Math.max(maxOwed, rec[i].owed);
    if (rec[i].owed >= 6) stalls++;
    if (rec[i].job) jobFrames++;
}
const avg = seq.reduce((a, b) => a + b, 0) / seq.length;
// judder: how far the displayed position strays from a perfectly even scroll
let pos = 0, dev2 = 0, maxDev = 0;
for (let i = 0; i < seq.length; i++) { pos += seq[i]; const e = pos - avg * (i + 1); maxDev = Math.max(maxDev, Math.abs(e)); }
const meanStep = avg; const rms = Math.sqrt(seq.reduce((s, d) => s + (d - meanStep) ** 2, 0) / seq.length);
const entAvg = rec.reduce((s, r) => s + r.ents, 0) / rec.length;
console.log(`variant ${variant} ${buttons || 'cruise'}: levels ${Math.min(...rec.map(r => r.lvl))}-${Math.max(...rec.map(r => r.lvl))}  avg ${avg.toFixed(2)} px/f  steps ${JSON.stringify(hist)}  late ${late} (${(100 * late / seq.length).toFixed(1)}%)  stepRMS ${rms.toFixed(2)}  owed>=6 ${stalls}  maxOwed ${maxOwed}  jobFrames ${(100 * jobFrames / seq.length).toFixed(0)}%  ents ${entAvg.toFixed(1)}`);
console.log(`  first 60 steps: ${seq.slice(100, 160).join('')}`);
