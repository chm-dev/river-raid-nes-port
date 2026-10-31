/*
 * perf_pass_trace.mjs - cost of each main-loop PASS (vsync return -> next vsync entry),
 * measured with the real CPU cycle counter, plus what the pass contained.
 *
 * perf_frame_trace.mjs resets per frame, which cuts a late pass in two.  This one
 * answers "what was in the passes that missed the frame?".  A pass is late when an
 * NMI landed inside it (sys_time changed); its NMI time is reported separately.
 *
 * Usage: node tests/perf_pass_trace.mjs <rom> <noi> <variant> <''|Up|Down> <frames>
 * Export statics (#define static) in river.c and entities.c for the group split.
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol } from './mcp_client.mjs';

const [rom, noi, variantArg, btn, framesArg] = process.argv.slice(2);
const SYM = symbolsFromNoi(path.resolve(noi));
const S = n => requireSymbol(SYM, n);
const variant = Number(variantArg || 0), buttons = btn || '', N = Number(framesArg || 400);

const GROUPS = ['nmi+lib', 'convert', 'job', 'render', 'gen_state', 'follow', 'player', 'collision', 'sound', 'hud', 'ent_update', 'ent_move', 'ent_draw', 'ent_hits', 'ent_row', 'bcall+other'];
const RULES = [
    [1, /^_river_(convert_row|copy_line)/],
    [2, /^_river_(run_job|emit_\w+|put|find_dup|margin_id|finish_tile_row)$|^_vp_|^_memset$/],
    [3, /^_river_(render_row|fill|fill_core|clear_row|copy_template|pattern|edge_black|road_paint)$/],
    [4, /^_river_|^_rs_|^_hud_set_playfield_scroll$|^__mulint$/],
    [5, /^_entities_follow$/], [6, /^_player_/], [7, /^_collision_/], [8, /^_sound_/], [9, /^_hud_/],
    [10, /^_entities_update$|^_ent_wake|^_ent_tank|^_ent_shell|^_ent_flash/], [11, /^_ent_move|^_ent_touching|^_ent_turn/],
    [12, /^_ent_draw/], [13, /^_entities_hit_shot|^_entities_check_player|^_ent_scan|^_ent_bridge|^_ent_shell_hits/],
    [14, /^_entities_row|^_entities_add|^_ent_/],
    [0, /^\.|^_wait_vbl|^_vsync|^_display|^_flush|^_identity|^_read_joypad|^_strobe|^_joypad/],
];
const entries = [], seen = new Set();
for (const [name, addr] of Object.entries(SYM)) {
    let pc = null;
    if (addr >= 0xC000 && addr <= 0xFFFF) pc = addr;
    else if (addr >= 0x18000 && addr <= 0x1BFFF) pc = addr - 0x10000;
    if (pc === null || seen.has(pc) || /_PARM_|^_atari_|^s__|^l__/.test(name)) continue;
    seen.add(pc);
    let g = 15;
    for (const [id, re] of RULES) if (re.test(name)) { g = id; break; }
    entries.push({ pc, g });
}
entries.sort((a, b) => a.pc - b.pc);
const t = `00:7 01:6 05:3 06:5 08:3 09:2 0A:2 0D:4 0E:6 10:2 11:5 15:4 16:6 18:2 19:4 1D:4 1E:7 20:6 21:6 24:3 25:3 26:5 28:4 29:2 2A:2 2C:4 2D:4 2E:6 30:2 31:5 35:4 36:6 38:2 39:4 3D:4 3E:7 40:6 41:6 45:3 46:5 48:3 49:2 4A:2 4C:3 4D:4 4E:6 50:2 51:5 55:4 56:6 58:2 59:4 5D:4 5E:7 60:6 61:6 65:3 66:5 68:4 69:2 6A:2 6C:5 6D:4 6E:6 70:2 71:5 75:4 76:6 78:2 79:4 7D:4 7E:7 81:6 84:3 85:3 86:3 88:2 8A:2 8C:4 8D:4 8E:4 90:2 91:6 94:4 95:4 96:4 98:2 99:5 9A:2 9D:5 A0:2 A1:6 A2:2 A4:3 A5:3 A6:3 A8:2 A9:2 AA:2 AC:4 AD:4 AE:4 B0:2 B1:5 B4:4 B5:4 B6:4 B8:2 B9:4 BA:2 BC:4 BD:4 BE:4 C0:2 C1:6 C4:3 C5:3 C6:5 C8:2 C9:2 CA:2 CC:4 CD:4 CE:6 D0:2 D1:5 D5:4 D6:6 D8:2 D9:4 DD:4 DE:7 E0:2 E1:6 E4:3 E5:3 E6:5 E8:2 E9:2 EA:2 EC:4 ED:4 EE:6 F0:2 F1:5 F5:4 F6:6 F8:2 F9:4 FD:4 FE:7`;
const cyc = Array(256).fill(3);
for (const p of t.split(' ')) { const [o, c] = p.split(':'); cyc[parseInt(o, 16)] = parseInt(c); }

const VSYNC = S('_wait_vbl_done');
const RETURN = VSYNC + 0x16;   // lda _oam_valid after the wait loop (see the disassembly)
const P = n => { const a = SYM['_' + n]; return a > 0xFFFF ? a - 0x10000 : a; };
const MAIL = 0x1C00;

const lua = `
local starts = {${entries.map(e => '0x' + e.pc.toString(16)).join(',')}}
local gids = {${entries.map(e => e.g).join(',')}}
local cyc = {${cyc.join(',')}}
local n = #starts
local cnt = {}
for i = 0, 15 do cnt[i] = 0 end
local lo, hi, cur = 1, 0, 15
local function bucket(pc)
  if pc >= lo and pc < hi then return cur end
  if pc < starts[1] then lo = 0; hi = starts[1]; cur = 15; return 15 end
  local a, b = 1, n
  while a < b do
    local m = (a + b + 1) // 2
    if starts[m] <= pc then a = m else b = m - 1 end
  end
  lo = starts[a]; hi = (a < n) and starts[a + 1] or 0x10000; cur = gids[a]
  return cur
end
local inPass = false
local t0, st0 = 0, 0
local rows, renders, ups, jobs, grp, seq = 0, 0, 0, 0, 0, 0
local feat, isl, pln = 0, 0, 0
local CR = emu.memType.nesChrRam
local function w16(a, v) if v > 65535 then v = 65535 end emu.write(a, v & 255, CR); emu.write(a + 1, v >> 8, CR) end
emu.addMemoryCallback(function(address, value)
  local b = bucket(address)
  cnt[b] = cnt[b] + cyc[value + 1]
end, emu.callbackType.exec, 0x8000, 0xFFFF)
emu.addMemoryCallback(function()
  t0 = emu.getState()["cpu.cycleCount"]
  st0 = emu.read(${S('sys_time')}, emu.memType.nesMemory)
  for i = 0, 15 do cnt[i] = 0 end
  rows, renders, ups, jobs, grp = 0, 0, 0, 0, 0
  feat, isl, pln = 0, 0, 0
  inPass = true
end, emu.callbackType.exec, ${RETURN}, ${RETURN})
emu.addMemoryCallback(function()
  if not inPass then return end
  inPass = false
  local dt = emu.getState()["cpu.cycleCount"] - t0
  local st = emu.read(${S('sys_time')}, emu.memType.nesMemory)
  local nmis = (st - st0) & 255
  seq = (seq + 1) & 255
  w16(${MAIL}, dt)
  for i = 0, 15 do w16(${MAIL} + 2 + i * 2, cnt[i]) end
  emu.write(${MAIL} + 34, nmis, CR)
  emu.write(${MAIL} + 35, rows, CR)
  emu.write(${MAIL} + 36, renders, CR)
  emu.write(${MAIL} + 37, ups, CR)
  emu.write(${MAIL} + 38, jobs, CR)
  emu.write(${MAIL} + 39, grp, CR)
  emu.write(${MAIL} + 40, emu.read(${S('entity_count')}, emu.memType.nesMemory), CR)
  emu.write(${MAIL} + 41, emu.read(${S('ent_oam_used')}, emu.memType.nesMemory), CR)
  emu.write(${MAIL} + 42, seq, CR)
  emu.write(${MAIL} + 43, feat, CR)
  emu.write(${MAIL} + 44, isl, CR)
  emu.write(${MAIL} + 45, pln, CR)
end, emu.callbackType.exec, ${VSYNC}, ${VSYNC})
emu.addMemoryCallback(function() rows = rows + 1 end, emu.callbackType.exec, ${P('river_generate_raw_row')}, ${P('river_generate_raw_row')})
emu.addMemoryCallback(function() renders = renders + 1 end, emu.callbackType.exec, ${P('river_render_row')}, ${P('river_render_row')})
emu.addMemoryCallback(function() ups = ups + 1 end, emu.callbackType.exec, ${P('river_emit_pattern')}, ${P('river_emit_pattern')})
emu.addMemoryCallback(function() jobs = jobs + 1 end, emu.callbackType.exec, ${P('river_run_job')}, ${P('river_run_job')})
emu.addMemoryCallback(function() grp = 1 end, emu.callbackType.exec, ${P('entities_update')}, ${P('entities_update')})
emu.addMemoryCallback(function()
  feat = feat + 1
end, emu.callbackType.exec, ${P('river_render_row')}, ${P('river_render_row')})
emu.addMemoryCallback(function() isl = isl + 1 end, emu.callbackType.exec, ${P('river_render_island')}, ${P('river_render_island')})
emu.addMemoryCallback(function() pln = pln + 1 end, emu.callbackType.exec, ${P('river_render_plain')}, ${P('river_render_plain')})
emu.log("armed")
`;

const step = async n => { for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' }); };
const press = async (b, n = 4) => { await callTool('mesen_input_override', { action: 'set', port: 0, buttons: b }); await step(n); await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' }); await step(4); };
await init();
await callTool('mesen_load_rom', { filepath: path.resolve(rom) });
await sleep(1500);
await callTool('mesen_playback', { action: 'pause' });
await step(60);
for (let i = 0; i < variant * 2; i++) await press('Up');
await press('Start', 8);
await writeBytes(S('player_invuln'), [1]);
await step(420);
await writeBytes(S('player_invuln'), [1]);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: 'A' });
await step(6);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons });
await step(240);
await callTool('mesen_run_lua_script', { code: lua, waitMs: 500, persistent: true });

const recs = [];
let prevSeq = null;
for (let k = 0; k < N / 20; k++) {
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: [k % 2 ? 'A' : '', buttons].filter(Boolean).join(',') });
    for (let i = 0; i < 20; i++) {
        await step(1);
        const m = await readBytes(MAIL, 46, 'NesChrRam');
        if (m[42] === prevSeq) continue;
        prevSeq = m[42];
        const u = o => m[o] | (m[o + 1] << 8);
        const g = []; for (let j = 0; j < 16; j++) g.push(u(2 + j * 2));
        recs.push({ dt: u(0), g, nmis: m[34], rows: m[35], renders: m[36], ups: m[37], jobs: m[38], grp: m[39], ents: m[40], oam: m[41], feat: m[43], isl: m[44], pln: m[45] });
    }
}
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });
recs.shift();

const NMI = 5700;
const work = r => r.dt - r.nmis * NMI;
const late = recs.filter(r => r.nmis > 0), ok = recs.filter(r => r.nmis === 0);
const avg = (rs, f) => rs.length ? rs.reduce((s, r) => s + f(r), 0) / rs.length : 0;
const maxOf = (rs, f) => rs.reduce((s, r) => Math.max(s, f(r)), 0);
const kinds = `rows ${avg(recs, r => r.rows).toFixed(2)}  feature ${avg(recs, r => r.feat).toFixed(2)}  island ${avg(recs, r => r.isl).toFixed(2)}  plain ${avg(recs, r => r.pln).toFixed(2)}  same ${avg(recs, r => r.rows - r.feat - r.isl - r.pln).toFixed(2)}`;
console.log(`row kinds per pass: ${kinds}`);
console.log(`\n=== variant ${variant} ${buttons || 'cruise'}: ${recs.length} passes, late ${late.length} (${(100 * late.length / recs.length).toFixed(1)}%)`);
console.log(`work cycles (true counter, NMI removed): on-time mean ${avg(ok, work).toFixed(0)} max ${maxOf(ok, work)}   late mean ${avg(late, work).toFixed(0)} max ${maxOf(late, work)}`);
for (const [name, rs] of [['A player', recs.filter(r => !r.grp)], ['B entities', recs.filter(r => r.grp)]]) {
    console.log(`  group ${name}: ${rs.length} passes, mean ${avg(rs, work).toFixed(0)}, max ${maxOf(rs, work)}, late ${rs.filter(r => r.nmis).length}, rows/pass ${avg(rs, r => r.rows).toFixed(2)}, late rows/pass ${avg(rs.filter(r => r.nmis), r => r.rows).toFixed(2)}`);
}
const byRows = {};
for (const r of recs) { const k = `${r.grp ? 'B' : 'A'}${r.rows}`; (byRows[k] = byRows[k] || []).push(r); }
console.log('  group+rows: count, mean work, late%');
for (const k of Object.keys(byRows).sort()) console.log(`    ${k}: n=${String(byRows[k].length).padStart(3)} work=${avg(byRows[k], work).toFixed(0).padStart(6)} late=${(100 * byRows[k].filter(r => r.nmis).length / byRows[k].length).toFixed(0)}%`);
console.log('  mean sampled cycles per group (sampler ~10-15% low):      on-time    late');
GROUPS.forEach((name, i) => console.log(`    ${name.padEnd(14)} ${avg(ok, r => r.g[i]).toFixed(0).padStart(26)} ${avg(late, r => r.g[i]).toFixed(0).padStart(8)}`));
console.log('  late passes (first 14): work rows renders ups grp ents oam | top groups');
for (const r of late.slice(0, 14)) {
    const top = r.g.map((v, k) => [k, v]).filter(([k]) => k > 0).sort((a, b) => b[1] - a[1]).slice(0, 5).map(([k, v]) => `${GROUPS[k]}=${v}`).join(' ');
    console.log(`    ${String(work(r)).padStart(6)} r${r.rows} rn${r.renders} u${r.ups} ${r.grp ? 'B' : 'A'} e${r.ents} o${r.oam} n${r.nmis} | ${top}`);
}
