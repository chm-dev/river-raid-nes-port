/*
 * pass_log.mjs - read the per-pass ring of the -ProfExport ROM (pl_rec / pl_idx
 * in gbc/main.c) after a few seconds of invulnerable play and print, for each
 * pass, how many scanlines the river part and the logic part used.
 *
 *   node tests/gb/pass_log.mjs fast|cruise
 *
 * Columns: ticks = frames since the previous pass start, kind = 0 entities /
 * 1 player / 2 player+entities, river / logic / camOAM = scanlines used by that
 * part (154 per frame).  Pass start LY is just after VBlank began (~144).
 */
import { readFileSync } from 'fs';
import * as c from './gearboy_client.mjs';

const mode = process.argv[2] || 'fast';
const txt = r => r.map(x => x.text || '').join('');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const noi = readFileSync(process.env.GB_NOI || 'roms/river_raid.noi', 'utf8');
const sym = n => parseInt(new RegExp(`DEF ${n} 0x([0-9A-Fa-f]+)`).exec(noi)[1], 16);
const hex = n => (n - 0xC000).toString(16).toUpperCase().padStart(4, '0');

await c.init();
await c.call('set_profiler', { action: 'start' });
await c.call('debug_continue');
await sleep(3000);
await c.call('controller_button', { action: 'press', button: 'start', player: 1 });
await sleep(300);
await c.call('controller_button', { action: 'release', button: 'start', player: 1 });
await sleep(4000);
await c.call('write_memory', { area: 6, offset: hex(sym('_player_invuln')), bytes: '01' });
if (mode === 'fast') await c.call('controller_button', { action: 'press', button: 'up', player: 1 });
await sleep(6000);
await c.call('debug_pause').catch(() => {});
const rd = async (addr, size) => JSON.parse(txt(await c.call('read_memory', { area: 6, offset: hex(addr), size })));
const idx = parseInt((await rd(sym('_pl_idx'), 1)).data, 16);
const raw = (await rd(sym('_pl_rec'), 640)).data.replace(/\s+/g, '');
const rec = [];
for (let i = 0; i < 64; i++) {
    const r = [];
    for (let j = 0; j < 10; j++) r.push(parseInt(raw.substr((i * 10 + j) * 2, 2), 16));
    rec.push(r);
}
const ord = [];
for (let k = 0; k < 64; k++) ord.push(rec[(idx + k) & 63]);
if (/DEF _pl_oam_err /.test(noi)) {      /* -OamCheck build */
    const n = (await rd(sym('_pl_oam_n'), 2)).data.replace(/\s+/g, '');
    const e = (await rd(sym('_pl_oam_err'), 2)).data.replace(/\s+/g, '');
    const le = h => parseInt(h.substr(2, 2) + h.substr(0, 2), 16);
    console.log(`oam_camera check: ${le(n)} passes compared, ${le(e)} mismatches`);
}
const adv =(a, b) => (b - a + 154) % 154;            /* lines from LY a to LY b, < 1 frame */
console.log('ticks kind  river logic camOAM  total  owed(before>after)');
let n1 = 0, tot = 0;
const sums = { 0: [0, 0], 1: [0, 0], 2: [0, 0] };
for (const r of ord.slice(0, 63)) {
    const [t, ly0, lyR, dtR, lyL, kind, lyE, , ow0, ow1] = r;
    const river = dtR * 154 + adv(ly0, lyR);
    const logic = adv(lyR, lyL);
    const cam = adv(lyL, lyE);
    const total = river + logic + cam;
    console.log(String(t).padStart(5), String(kind).padStart(4), String(river).padStart(6), String(logic).padStart(5),
        String(cam).padStart(6), String(total).padStart(6), String(ow0 + '>' + ow1).padStart(8), total > 154 ? '  OVER' : '');
    sums[kind][0] += total; sums[kind][1]++;
}
for (const k of [0, 1, 2]) if (sums[k][1]) console.log(`kind ${k}: n ${sums[k][1]} avg ${(sums[k][0] / sums[k][1]).toFixed(0)} lines`);
const hist = {};
for (const r of ord.slice(0, 63)) hist[r[0]] = (hist[r[0]] || 0) + 1;
console.log('ticks histogram', JSON.stringify(hist));
