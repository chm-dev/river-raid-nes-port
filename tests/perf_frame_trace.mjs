/*
 * perf_frame_trace.mjs - per-frame cost trace and what the heavy frames contain.
 *
 * Like perf_function_cycles.mjs, but resets the counters every frame and reads
 * one record per stepped frame (cycles per group: convert, job, render,
 * entities...).  A frame where frame_counter does not advance is "late": the
 * main-loop pass spans two frames.  Prints the late share, mean cost of normal
 * vs late frames and the 12 heaviest frames with their top groups, which is how
 * the tile-job spike and the entity draw cost were found.
 *
 * Run:  node tests/perf_frame_trace.mjs [cruise|fast|slow] [frames]
 * Restart MesenCE-MCP between runs (persistent Lua scripts survive reloads).
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol } from './mcp_client.mjs';

const ROM = path.resolve('roms/river_raid.nes');
const SYM = symbolsFromNoi(path.resolve('roms/river_raid.noi'));
const S = n => requireSymbol(SYM, n);
const scenarioName = process.argv[2] || 'cruise';
const buttons = scenarioName === 'fast' ? 'Up' : scenarioName === 'slow' ? 'Down' : '';
const FRAMES = Number(process.argv[3] || 900);

const GROUPS = ['idle', 'nmi_spin', 'convert', 'job', 'render', 'gen_misc', 'player', 'hud', 'ent_spawn', 'ent_row', 'ent_shot', 'ent_block(move/draw)', 'ent_update', 'collision/sound', 'other'];
const RULES = [
    [0, /^_wait_vbl_done$/], [1, /^\.delay_to_lcd_scanline$/],
    [2, /^_river_(convert_row|copy_line)/],
    [3, /^_river_(run_job|emit_\w+|put|pat_equal|find_dup|margin_id|finish_tile_row)$|^_vp_/],
    [4, /^_river_(render_row|fill|fill_core|clear_row|copy_template|pattern|edge_black|road_paint)$/],
    [5, /^_river_|^_hud_set_playfield_scroll$|^_memset$|^__mulint$|^___memcpy$|^__divuint$/],
    [6, /^_player_/], [7, /^_hud_/],
    [8, /^_entities_add_score$/], [9, /^_entities_row$/], [10, /^_entities_hit_shot$/],
    [11, /^_entities_check_player$/], [12, /^_entities_update$/],
    [13, /^_(collision|sound)_update$/],
];
const entries = [];
for (const [name, addr] of Object.entries(SYM)) {
    let pc = null;
    if (addr >= 0xC000 && addr <= 0xFFFF) pc = addr;
    else if (addr >= 0x18000 && addr <= 0x1BFFF) pc = addr - 0x10000;
    if (pc === null || /^s__|^l__|_PARM_|^_atari_/.test(name)) continue;
    let g = 14;
    for (const [id, re] of RULES) if (re.test(name)) { g = id; break; }
    entries.push({ pc, g });
}
entries.sort((a, b) => a.pc - b.pc);
const starts = entries.map(e => '0x' + e.pc.toString(16)).join(',');
const gids = entries.map(e => e.g).join(',');

const t = `00:7 01:6 05:3 06:5 08:3 09:2 0A:2 0D:4 0E:6 10:2 11:5 15:4 16:6 18:2 19:4 1D:4 1E:7 20:6 21:6 24:3 25:3 26:5 28:4 29:2 2A:2 2C:4 2D:4 2E:6 30:2 31:5 35:4 36:6 38:2 39:4 3D:4 3E:7 40:6 41:6 45:3 46:5 48:3 49:2 4A:2 4C:3 4D:4 4E:6 50:2 51:5 55:4 56:6 58:2 59:4 5D:4 5E:7 60:6 61:6 65:3 66:5 68:4 69:2 6A:2 6C:5 6D:4 6E:6 70:2 71:5 75:4 76:6 78:2 79:4 7D:4 7E:7 81:6 84:3 85:3 86:3 88:2 8A:2 8C:4 8D:4 8E:4 90:2 91:6 94:4 95:4 96:4 98:2 99:5 9A:2 9D:5 A0:2 A1:6 A2:2 A4:3 A5:3 A6:3 A8:2 A9:2 AA:2 AC:4 AD:4 AE:4 B0:2 B1:5 B4:4 B5:4 B6:4 B8:2 B9:4 BA:2 BC:4 BD:4 BE:4 C0:2 C1:6 C4:3 C5:3 C6:5 C8:2 C9:2 CA:2 CC:4 CD:4 CE:6 D0:2 D1:5 D5:4 D6:6 D8:2 D9:4 DD:4 DE:7 E0:2 E1:6 E4:3 E5:3 E6:5 E8:2 E9:2 EA:2 EC:4 ED:4 EE:6 F0:2 F1:5 F5:4 F6:6 F8:2 F9:4 FD:4 FE:7`;
const cyc = Array(256).fill(3);
for (const p of t.split(' ')) { const [o, c] = p.split(':'); cyc[parseInt(o, 16)] = parseInt(c); }

const lua = `
local starts = {${starts}}
local gids = {${gids}}
local cyc = {${cyc.join(',')}}
local n = #starts
local cnt = {}
for i = 0, 15 do cnt[i] = 0 end
local seq = 0
local lo, hi, cur = 1, 0, 14
local function bucket(pc)
  if pc >= lo and pc < hi then return cur end
  if pc < starts[1] then lo = 0; hi = starts[1]; cur = 14; return 14 end
  local a, b = 1, n
  while a < b do
    local m = (a + b + 1) // 2
    if starts[m] <= pc then a = m else b = m - 1 end
  end
  lo = starts[a]; hi = (a < n) and starts[a + 1] or 0x10000; cur = gids[a]
  return cur
end
emu.addMemoryCallback(function(address, value)
  local b = bucket(address)
  cnt[b] = cnt[b] + cyc[value + 1]
end, emu.callbackType.exec, 0x8000, 0xFFFF)
local function rd(a) return emu.read(a, emu.memType.nesMemory) end
emu.addEventCallback(function()
  seq = seq + 1
  for i = 0, 15 do
    local v = cnt[i]; if v > 65535 then v = 65535 end
    emu.write(0x1F00 + i * 2, v & 255, emu.memType.nesChrRam)
    emu.write(0x1F00 + i * 2 + 1, v >> 8, emu.memType.nesChrRam)
    cnt[i] = 0
  end
  emu.write(0x1F20, rd(${S('frame_counter')}), emu.memType.nesChrRam)
  emu.write(0x1F21, rd(${S('frame_counter') + 1}), emu.memType.nesChrRam)
  emu.write(0x1F22, rd(${S('river_owed')}), emu.memType.nesChrRam)
  emu.write(0x1F23, rd(${S('river_scrolled')}), emu.memType.nesChrRam)
  emu.write(0x1F24, rd(${S('river_job')}), emu.memType.nesChrRam)
  emu.write(0x1F25, rd(${S('river_block_index')}), emu.memType.nesChrRam)
  emu.write(0x1F26, rd(${S('entity_count')}), emu.memType.nesChrRam)
  emu.write(0x1F27, seq & 255, emu.memType.nesChrRam)
end, emu.eventType.endFrame)
emu.log("armed")
`;

async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}

await init();
await callTool('mesen_load_rom', { filepath: ROM });
await sleep(1500);
await callTool('mesen_playback', { action: 'pause' });
await writeBytes(S('player_invuln'), [1]);
await step(420);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: 'A' });
await step(6);
await callTool('mesen_input_override', { action: 'set', port: 0, buttons });
await step(240);
await callTool('mesen_run_lua_script', { code: lua, waitMs: 500, persistent: true });

const recs = [];
let prevFc = null, prevSeq = null;
for (let f = 0; f < FRAMES; f++) {
    if (f % 20 === 0 && f > 0) {   // keep firing now and then, as in the earlier profiles
        const b = ((f / 20) % 2 ? 'A,' : '') + buttons;
        await callTool('mesen_input_override', { action: 'set', port: 0, buttons: b.replace(/,$/, '') });
    }
    await step(1);
    const m = await readBytes(0x1F00, 48, 'NesChrRam');
    const g = []; for (let i = 0; i < 15; i++) g.push(m[i * 2] | (m[i * 2 + 1] << 8));
    const fc = m[0x20] | (m[0x21] << 8);
    const seq = m[0x27];
    if (prevSeq !== null && seq === prevSeq) continue;  // no fresh record
    recs.push({ g, dfc: prevFc === null ? 1 : ((fc - prevFc) & 0xFFFF), owed: m[0x22], scrolled: m[0x23], job: m[0x24], blk: m[0x25], ents: m[0x26] });
    prevFc = fc; prevSeq = seq;
}
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });

const work = r => r.g.reduce((s, v, i) => s + (i > 1 ? v : 0), 0);
const late = recs.filter(r => r.dfc === 0);          // main-loop pass still running: no new frame_counter value
const ok = recs.filter(r => r.dfc !== 0);
const mean = (rs, i) => rs.length ? rs.reduce((s, r) => s + r.g[i], 0) / rs.length : 0;
console.log(`\n=== ${scenarioName}: ${recs.length} frames; frame_counter did not advance on ${late.length} (${(100 * late.length / recs.length).toFixed(1)}%)`);
console.log(`mean work (excl. idle & NMI spin): all ${(recs.reduce((s, r) => s + work(r), 0) / recs.length).toFixed(0)}  normal ${(ok.reduce((s, r) => s + work(r), 0) / ok.length).toFixed(0)}  late ${late.length ? (late.reduce((s, r) => s + work(r), 0) / late.length).toFixed(0) : '-'}`);
console.log('\nmean cycles per group:            normal frames    late frames');
for (let i = 2; i < 15; i++) console.log(`  ${GROUPS[i].padEnd(24)} ${mean(ok, i).toFixed(0).padStart(8)}   ${mean(late, i).toFixed(0).padStart(8)}`);

// a late frame is the end of an overrun; the previous frame holds the work that caused it
console.log('\nworst 12 frames by work:');
const idx = recs.map((r, i) => [i, work(r)]).sort((a, b) => b[1] - a[1]).slice(0, 12);
for (const [i, w] of idx) {
    const r = recs[i];
    const top = r.g.map((v, k) => [k, v]).filter(([k]) => k > 1).sort((a, b) => b[1] - a[1]).slice(0, 4).map(([k, v]) => `${GROUPS[k].split('(')[0]}=${v}`).join(' ');
    console.log(`  #${String(i).padStart(3)} work ${String(w).padStart(5)} idle ${String(r.g[0]).padStart(5)} dfc ${r.dfc} job ${r.job} blk ${r.blk} owed ${r.owed} scrl ${r.scrolled}  ${top}`);
}
// how often does an overrun follow each kind of heavy frame
const bySeq = {};
for (let i = 0; i < recs.length - 1; i++) {
    const w = work(recs[i]);
    const k = w > 24000 ? '>24k' : w > 20000 ? '20-24k' : w > 16000 ? '16-20k' : '<=16k';
    bySeq[k] = bySeq[k] || { n: 0, late: 0 };
    bySeq[k].n++; if (recs[i + 1].dfc === 0 || recs[i].dfc === 0) bySeq[k].late++;
}
console.log('\nwork per frame vs frames adjacent to a late frame:', JSON.stringify(bySeq));
