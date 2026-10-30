/*
 * prof_level.mjs - per-function cycle table for one start level and throttle, over
 * every symbol of the ROM (static functions must be exported in the scratch build).
 * Usage: node prof_level.mjs <rom> <noi> <variant> <''|Up|Down> <frames>
 * Restart Mesen between runs: persistent Lua scripts survive reloads.
 */
import path from 'node:path';
import { init, callTool, readBytes, writeBytes, sleep, symbolsFromNoi, requireSymbol } from './mcp_client.mjs';
const [rom, noi, variantArg, btn, framesArg] = process.argv.slice(2);
const SYM = symbolsFromNoi(path.resolve(noi));
const S = n => requireSymbol(SYM, n);
const variant = Number(variantArg || 0), buttons = btn || '', N = Number(framesArg || 300);
const entries = [];
const seen = new Map();
for (const [name, addr] of Object.entries(SYM)) {
    let pc = null;
    if (addr >= 0xC000 && addr <= 0xFFFF) pc = addr;
    else if (addr >= 0x18000 && addr <= 0x1BFFF) pc = addr - 0x10000;
    if (pc === null || /_PARM_|^_atari_|^_ent_gfx|^s__|^l__|^_river_lut/.test(name)) continue;
    if (seen.has(pc)) continue;
    seen.set(pc, name); entries.push({ name, pc });
}
entries.sort((a, b) => a.pc - b.pc);
const idx = entries.map((_, i) => Math.min(i + 1, 250));
const starts = entries.map(e => '0x' + e.pc.toString(16)).join(',');
const t = `00:7 01:6 05:3 06:5 08:3 09:2 0A:2 0D:4 0E:6 10:2 11:5 15:4 16:6 18:2 19:4 1D:4 1E:7 20:6 21:6 24:3 25:3 26:5 28:4 29:2 2A:2 2C:4 2D:4 2E:6 30:2 31:5 35:4 36:6 38:2 39:4 3D:4 3E:7 40:6 41:6 45:3 46:5 48:3 49:2 4A:2 4C:3 4D:4 4E:6 50:2 51:5 55:4 56:6 58:2 59:4 5D:4 5E:7 60:6 61:6 65:3 66:5 68:4 69:2 6A:2 6C:5 6D:4 6E:6 70:2 71:5 75:4 76:6 78:2 79:4 7D:4 7E:7 81:6 84:3 85:3 86:3 88:2 8A:2 8C:4 8D:4 8E:4 90:2 91:6 94:4 95:4 96:4 98:2 99:5 9A:2 9D:5 A0:2 A1:6 A2:2 A4:3 A5:3 A6:3 A8:2 A9:2 AA:2 AC:4 AD:4 AE:4 B0:2 B1:5 B4:4 B5:4 B6:4 B8:2 B9:4 BA:2 BC:4 BD:4 BE:4 C0:2 C1:6 C4:3 C5:3 C6:5 C8:2 C9:2 CA:2 CC:4 CD:4 CE:6 D0:2 D1:5 D5:4 D6:6 D8:2 D9:4 DD:4 DE:7 E0:2 E1:6 E4:3 E5:3 E6:5 E8:2 E9:2 EA:2 EC:4 ED:4 EE:6 F0:2 F1:5 F5:4 F6:6 F8:2 F9:4 FD:4 FE:7`;
const cyc = Array(256).fill(3);
for (const p of t.split(' ')) { const [o, c] = p.split(':'); cyc[parseInt(o, 16)] = parseInt(c); }
const lua = `
local starts = {${starts}}
local cyc = {${cyc.join(',')}}
local n = #starts
local cnt = {}
for i = 0, 1023 do cnt[i] = 0 end
local frames = 0
local lo, hi, cur = 1, 0, 0
local function bucket(pc)
  if pc >= lo and pc < hi then return cur end
  if pc < starts[1] then lo = 0; hi = starts[1]; cur = 0; return 0 end
  local a, b = 1, n
  while a < b do
    local m = (a + b + 1) // 2
    if starts[m] <= pc then a = m else b = m - 1 end
  end
  lo = starts[a]; hi = (a < n) and starts[a + 1] or 0x10000; cur = a
  return cur
end
emu.addMemoryCallback(function(address, value)
  local b = bucket(address)
  cnt[b] = cnt[b] + cyc[value + 1]
end, emu.callbackType.exec, 0x8000, 0xFFFF)
emu.addEventCallback(function()
  frames = frames + 1
  for i = 0, 340 do
    local v = cnt[i] // 64
    if v > 65535 then v = 65535 end
    emu.write(0x1C00 + i * 2, v & 255, emu.memType.nesChrRam)
    emu.write(0x1C00 + i * 2 + 1, v >> 8, emu.memType.nesChrRam)
  end
  emu.write(0x1FFE, frames & 255, emu.memType.nesChrRam)
  emu.write(0x1FFF, (frames >> 8) & 255, emu.memType.nesChrRam)
end, emu.eventType.endFrame)
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
for (let k = 0; k < N / 20; k++) {
    await callTool('mesen_input_override', { action: 'set', port: 0, buttons: [k % 2 ? 'A' : '', buttons].filter(Boolean).join(',') });
    await step(20);
}
await callTool('mesen_input_override', { action: 'set', port: 0, buttons: '' });
const mb = await readBytes(0x1C00, 1024, 'NesChrRam');
const frames = mb[0x3FE] | (mb[0x3FF] << 8);
const rows = [];
let total = 0;
for (let i = 1; i <= Math.min(340, entries.length); i++) {
    const v = (mb[i * 2] | (mb[i * 2 + 1] << 8)) * 64 / frames;
    if (v > 0) { rows.push([entries[i - 1].name, v]); total += v; }
}
rows.sort((a, b) => b[1] - a[1]);
console.log(`== variant ${variant} ${buttons || 'cruise'}: ${frames} frames, ${entries.length} symbols, attributed ${total.toFixed(0)} cycles/frame`);
for (const [n, c] of rows.slice(0, 42)) console.log(`  ${n.padEnd(34)} ${c.toFixed(0).padStart(6)}  ${(100 * c / total).toFixed(1).padStart(5)}%`);
