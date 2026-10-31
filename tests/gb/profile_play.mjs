/*
 * profile_play.mjs - profile the GBC ROM in play: boot, Start, invulnerable jet,
 * optional Up (fast speed), then 10 s of profiler data sorted by max.
 *
 *   gearboy/Gearboy.exe --headless --mcp-http --mcp-http-port 7777 roms/river_raid.gb roms/river_raid.noi
 *   node tests/gb/profile_play.mjs fast|cruise [count]
 *
 * Build with .\build_gbc.ps1 -ProfExport so static functions have names.
 * Profiler units are 70,224 per frame at any CPU speed: M-cycles at double
 * speed = units / 2.  The headline number is passes (wait_vbl_done calls)
 * per frame: 1.0 = 60 Hz, 0.5 = the main loop locked at 30 Hz.
 */
import { readFileSync } from 'fs';
import * as c from './gearboy_client.mjs';

const mode = process.argv[2] || 'fast';
const count = Number(process.argv[3] || 24);
const txt = r => r.map(x => x.text || '').join('');
const sleep = ms => new Promise(r => setTimeout(r, ms));

await c.init();
await c.call('set_profiler', { action: 'start' });
await c.call('debug_continue');
await sleep(3000);                               /* boot: Start before this is ignored */
await c.call('controller_button', { action: 'press', button: 'start', player: 1 });
await sleep(300);
await c.call('controller_button', { action: 'release', button: 'start', player: 1 });
await sleep(4000);
/* _player_invuln: its address moves with every WRAM change, so read the .noi. */
const noi = readFileSync(process.env.GB_NOI || 'roms/river_raid.noi', 'utf8');
const inv = parseInt(/DEF _player_invuln 0x([0-9A-Fa-f]+)/.exec(noi)[1], 16);
await c.call('write_memory', { area: 6, offset: (inv - 0xC000).toString(16).toUpperCase().padStart(4, '0'), bytes: '01' });
if (mode === 'fast') await c.call('controller_button', { action: 'press', button: 'up', player: 1 });
await sleep(2500);
await c.call('set_profiler', { action: 'reset' });
await sleep(10000);
const d = JSON.parse(txt(await c.call('get_profiler_data', { count, sort: 'max' })));
const rd = name => c.call('read_memory', { area: 6, offset: (parseInt(new RegExp(`DEF ${name} 0x([0-9A-Fa-f]+)`).exec(noi)[1], 16) - 0xC000)
    .toString(16).toUpperCase().padStart(4, '0'), size: 1 }).then(r => txt(r));
console.log('river_clamps', await rd('_river_clamps'), 'river_owed_max', await rd('_river_owed_max'));
const w = d.functions.find(f => f.name === '_wait_vbl_done');
console.log(`mode ${mode}: frames ${d.frames}, passes ${w ? w.calls : '?'}, passes/frame ${w ? (w.calls / d.frames).toFixed(2) : '?'}`);
for (const f of d.functions) {
    console.log(f.name.padEnd(26), 'calls', String(f.calls).padStart(5), 'avg', String(f.average_cycles).padStart(7),
        'max', String(f.max_cycles).padStart(7));
}
