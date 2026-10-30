/*
 * perf_function_cycles.mjs - estimated cycles per frame by function.
 *
 * Arms a persistent Lua exec callback over $8000-$FFFF that adds each executed
 * instruction's base 6502 cycle cost to the function (symbol range) it is in,
 * and publishes the totals through unused CHR-RAM ($1F00).  Numbers are
 * instruction-count estimates (taken branches and page crossings are ignored),
 * roughly 10-15% low.  Banked code only has its public functions as symbols, so
 * a bucket covers the statics up to the next public function.
 *
 * Run:  node tests/perf_function_cycles.mjs [named|rest] [fast]
 *   named = the game's own functions (default), rest = everything else
 *   (wait loop, NMI split spin, library); add `fast` to hold Up.
 * IMPORTANT: persistent Lua scripts survive mesen_load_rom - restart
 * MesenCE-MCP between runs or old scripts keep overwriting the mailbox.
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol } from './mcp_client.mjs';

const ROM = path.resolve('roms/river_raid.nes');
const SYM = symbolsFromNoi(path.resolve('roms/river_raid.noi'));
const S = n => requireSymbol(SYM, n);
const FRAMES = 300;
const NAMED = /^_(river_|ent_|entities_|player_|hud_|sp_|vsync|set_|memset|memcpy|_mulint|_divuint|_modu|move|nmi|main|collision_|sound_|oam|shadow)/;

// Build the bucket table: every symbol in the fixed bank (CPU $C000+) and
// bank 1 ($8000+, linked at $18000), named ones get an index.
const entries = [];
for (const [name, addr] of Object.entries(SYM)) {
    let pc = null;
    if (addr >= 0xC000 && addr <= 0xFFFF) pc = addr;
    else if (addr >= 0x18000 && addr <= 0x1BFFF) pc = addr - 0x10000;
    if (pc !== null) entries.push({ name, pc });
}
entries.sort((a, b) => a.pc - b.pc);
let nextIdx = 1;
const names = ['(other)'];
for (const e of entries) {
    if ((process.argv[2] === 'rest' ? !NAMED.test(e.name) : NAMED.test(e.name)) && !/_PARM_|^_atari_|^s__|^l__/.test(e.name) && nextIdx < 126) { e.idx = nextIdx++; names.push(e.name); }
    else e.idx = 0;
}
const starts = entries.map(e => '0x' + e.pc.toString(16)).join(',');
const idxs = entries.map(e => e.idx).join(',');

const CYC = {};
const t = `00:7 01:6 05:3 06:5 08:3 09:2 0A:2 0D:4 0E:6 10:2 11:5 15:4 16:6 18:2 19:4 1D:4 1E:7 20:6 21:6 24:3 25:3 26:5 28:4 29:2 2A:2 2C:4 2D:4 2E:6 30:2 31:5 35:4 36:6 38:2 39:4 3D:4 3E:7 40:6 41:6 45:3 46:5 48:3 49:2 4A:2 4C:3 4D:4 4E:6 50:2 51:5 55:4 56:6 58:2 59:4 5D:4 5E:7 60:6 61:6 65:3 66:5 68:4 69:2 6A:2 6C:5 6D:4 6E:6 70:2 71:5 75:4 76:6 78:2 79:4 7D:4 7E:7 81:6 84:3 85:3 86:3 88:2 8A:2 8C:4 8D:4 8E:4 90:2 91:6 94:4 95:4 96:4 98:2 99:5 9A:2 9D:5 A0:2 A1:6 A2:2 A4:3 A5:3 A6:3 A8:2 A9:2 AA:2 AC:4 AD:4 AE:4 B0:2 B1:5 B4:4 B5:4 B6:4 B8:2 B9:4 BA:2 BC:4 BD:4 BE:4 C0:2 C1:6 C4:3 C5:3 C6:5 C8:2 C9:2 CA:2 CC:4 CD:4 CE:6 D0:2 D1:5 D5:4 D6:6 D8:2 D9:4 DD:4 DE:7 E0:2 E1:6 E4:3 E5:3 E6:5 E8:2 E9:2 EA:2 EC:4 ED:4 EE:6 F0:2 F1:5 F5:4 F6:6 F8:2 F9:4 FD:4 FE:7`;
const cycTable = Array.from({ length: 256 }, (_, i) => 3);
for (const p of t.split(' ')) { const [o, c] = p.split(':'); cycTable[parseInt(o, 16)] = parseInt(c); }

const lua = `
local starts = {${starts}}
local idxs = {${idxs}}
local cyc = {${cycTable.join(',')}}
local n = #starts
local cnt = {}
for i = 0, 127 do cnt[i] = 0 end
local frames = 0
local cc0 = nil
local ccAvg = 0
local lo, hi, cur = 1, 0, 0
local function bucket(pc)
  if pc >= lo and pc < hi then return cur end
  local a, b = 1, n
  if pc < starts[1] then lo = 0; hi = starts[1]; cur = 0; return 0 end
  while a < b do
    local m = (a + b + 1) // 2
    if starts[m] <= pc then a = m else b = m - 1 end
  end
  lo = starts[a]; hi = (a < n) and starts[a + 1] or 0x10000; cur = idxs[a]
  return cur
end
emu.addMemoryCallback(function(address, value)
  local b = bucket(address)
  cnt[b] = cnt[b] + cyc[value + 1]
end, emu.callbackType.exec, 0x8000, 0xFFFF)
emu.addEventCallback(function()
  frames = frames + 1
  local cc = emu.getState()['cpu.cycleCount']
  if cc0 == nil then cc0 = cc end
  if frames > 1 then ccAvg = (cc - cc0) // (frames - 1) end
  emu.write(0x1FFC, ccAvg & 255, emu.memType.nesChrRam)
  emu.write(0x1FFD, (ccAvg >> 8) & 255, emu.memType.nesChrRam)
  for i = 0, 126 do
    local v = cnt[i] // 256
    if v > 65535 then v = 65535 end
    emu.write(0x1F00 + i * 2, v & 255, emu.memType.nesChrRam)
    emu.write(0x1F00 + i * 2 + 1, v >> 8, emu.memType.nesChrRam)
  end
  emu.write(0x1FFE, frames & 255, emu.memType.nesChrRam)
  emu.write(0x1FFF, (frames >> 8) & 255, emu.memType.nesChrRam)
end, emu.eventType.endFrame)
emu.log("profiler armed")
`;

async function step(n) {
    for (let i = 0; i < n; i++) await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}

async function scenario(name, buttons) {
    await callTool('mesen_load_rom', { filepath: ROM });
    await sleep(1500);
    await callTool('mesen_playback', { action: 'pause' });
    await writeBytes(S('player_invuln'), [1]);
    await step(420);
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: 'A' });
    await step(6);
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons });
    await step(240);
    const r = await callTool('mesen_run_lua_script', { code: lua, waitMs: 500, persistent: true });
    const id = (r.match(/#(\d+)/) || [])[1];
    await step(FRAMES);
    const mb = await readBytes(0x1F00, 256, 'NesChrRam');
    if (id) { try { await callTool('mesen_remove_lua_script', { scriptId: Number(id) }); } catch { } }
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });
    const frames = mb[254] | (mb[255] << 8);
    console.log('  measured cpu cycles/frame (Mesen counter):', mb[252] | (mb[253] << 8));
    const rows = [];
    let total = 0;
    for (let i = 0; i < 127; i++) {
        const v = (mb[i * 2] | (mb[i * 2 + 1] << 8)) * 256;
        total += v;
        if (v) rows.push([names[i] || ('#' + i), v / frames]);
    }
    rows.sort((a, b) => b[1] - a[1]);
    console.log(`\n== ${name}: ${frames} frames profiled, total ${(total / frames).toFixed(0)} cycles/frame (frame = 29780)`);
    for (const [n, c] of rows.slice(0, 22)) console.log(`  ${n.padEnd(26)} ${c.toFixed(0).padStart(7)}  ${(100 * c / (total / frames)).toFixed(1)}%`);
}

await init();
if (process.argv[3] !== 'fast') await scenario('cruise', '');
if (process.argv[2] !== 'rest' || process.argv[3] === 'fast') await scenario('fast(Up)', 'Up');
