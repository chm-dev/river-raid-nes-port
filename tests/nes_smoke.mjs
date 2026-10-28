/*
 * Phase 2 NES smoke test (MesenCE-MCP, Streamable HTTP).
 *
 * Start Mesen before running:
 *   .\MesenCE-MCP\MesenCE-MCP-Windows.exe --mcp --mcp-port=9100 \
 *       --enableStdout --doNotSaveSettings --nes.region=Ntsc
 *
 *   node tests/nes_smoke.mjs
 *
 * Verifies:
 *   - GBDK mapper-30 / CHR-RAM / iNES 1.0 header contract,
 *   - the shared palette and the 128-glyph Atari charset in CHR-RAM,
 *   - the boot-screen text and deterministic scene rows in the nametable,
 *   - VBL/LCD handler cadence and the scanline-24 HUD split scaffolding,
 *   - rendered split geometry from the screenshot (HUD rows, blank row 3,
 *     status/river/bridge/footer positions),
 *   - the frame counter/BCD digit update and a clean 256x240 screenshot.
 */
import fs from 'node:fs';
import path from 'node:path';

import {
    init,
    callTool,
    readBytes,
    readU16,
    readU8,
    symbolsFromNoi,
    requireSymbol,
    sleep,
} from './mcp_client.mjs';
import { readPng, countRow, rowMatches } from './png.mjs';

const ROM = path.resolve('roms/river_raid.nes');
const NOI = path.resolve('roms/river_raid.noi');
const SHOT = path.resolve('build/boot.png');

const TILE_FIRST_SCENE = 128;
const TILE_LAND = 128;
const TILE_WATER = 129;
const TILE_BANK_LEFT = 130;
const TILE_BANK_RIGHT = 131;
const TILE_BRIDGE = 132;
const GLYPH_0 = 80;

let failures = 0;

function check(name, ok, detail = '') {
    console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
    if (!ok) failures++;
}

function eq(a, b) {
    return a.length === b.length && a.every((v, i) => v === b[i]);
}

function hexArray(a) {
    return a.map(v => v.toString(16).padStart(2, '0')).join(' ');
}

function glyphForChar(ch) {
    if (ch >= 'A' && ch <= 'Z') return ch.charCodeAt(0) - 65 + 1;
    if (ch >= 'a' && ch <= 'z') return ch.charCodeAt(0) - 97 + 33;
    if (ch >= '0' && ch <= '9') return ch.charCodeAt(0) - 48 + GLYPH_0;
    if (ch === '.') return 63;
    if (ch === '!') return 65;
    if (ch === '?') return 95;
    return 0;
}

function textGlyphs(text) {
    return [...text].map(glyphForChar);
}

function expectedRiverRow(relativeRow) {
    const waveTable = [0, 1, 2, 3, 4, 3, 2, 1];
    const y = 8 + relativeRow;
    const wave = waveTable[relativeRow & 7];
    const left = 3 + wave;
    const right = 28 - wave;
    const out = [];

    for (let x = 0; x < 32; x++) {
        if (x < left) out.push(TILE_LAND);
        else if (x === left) out.push(TILE_BANK_LEFT);
        else if (x < right) out.push(y === 16 ? TILE_BRIDGE : TILE_WATER);
        else if (x === right) out.push(TILE_BANK_RIGHT);
        else out.push(TILE_LAND);
    }
    return out;
}

function digitsValue(digits) {
    return digits.reduce((acc, d) => acc * 10 + (d - GLYPH_0), 0);
}

async function main() {
    if (!fs.existsSync(ROM)) throw new Error(`Missing ${ROM}`);
    if (!fs.existsSync(NOI)) throw new Error(`Missing ${NOI}`);

    const rom = fs.readFileSync(ROM);
    const prgBanks = rom[4];
    const chrBanks = rom[5];
    const mapper = ((rom[6] >> 4) & 0x0f) | (rom[7] & 0xf0);

    check('iNES magic', rom[0] === 0x4e && rom[1] === 0x45 &&
        rom[2] === 0x53 && rom[3] === 0x1a);
    check('iNES 1.0 (not NES 2.0)', (rom[7] & 0x0c) === 0,
        `byte7=0x${rom[7].toString(16)}`);
    check('mapper 30 (UNROM-512)', mapper === 30, `mapper=${mapper}`);
    check('CHR-RAM (0 CHR banks)', chrBanks === 0, `byte5=${chrBanks}`);
    check('PRG >= 2 x 16 KiB', prgBanks >= 2, `prgBanks=${prgBanks}`);
    check('file size matches header',
        rom.length === 16 + prgBanks * 16384 + chrBanks * 8192,
        `size=${rom.length}`);

    const sym = symbolsFromNoi(NOI);
    const symAddr = name => requireSymbol(sym, name);

    await init();

    await callTool('mesen_load_rom', { filepath: ROM });
    await sleep(900);
    await callTool('mesen_reset', { type: 'hard' });
    await sleep(1400);
    // A prior diagnostic may have left the debugger paused; the smoke test
    // needs the CPU running for the frame/LCD counters.
    try {
        await callTool('mesen_resume_execution', {});
    } catch {
        /* already running */
    }

    const header = JSON.parse(await callTool('mesen_get_rom_header'));
    check('Mesen sees mapper 30', header.mapper === 30,
        `mapper=${header.mapper}`);
    check('Mesen sees an iNES header', /ines/i.test(String(header.format)),
        `format=${header.format}`);

    // ---------------------------------------------------------------------
    // Palette and CHR-RAM
    // ---------------------------------------------------------------------
    const palette = await readBytes(0, 32, 'NesPaletteRam');
    check('shared BG palette 0 = 0F 30 1A 12',
        palette[0] === 0x0f && palette[1] === 0x30 &&
        palette[2] === 0x1a && palette[3] === 0x12,
        hexArray(palette.slice(0, 4)));

    const glyphA = await readBytes(1 * 16, 16, 'NesChrRam');
    check('1bpp glyph A expanded to NES 2bpp',
        eq(glyphA, [0x00, 0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]),
        hexArray(glyphA));

    const sceneChr = await readBytes(TILE_FIRST_SCENE * 16, 5 * 16, 'NesChrRam');
    const zeros = Array(8).fill(0x00);
    const ff = Array(8).fill(0xff);
    const lf = Array(8).fill(0x0f);
    const f0 = Array(8).fill(0xf0);
    const e7 = Array(8).fill(0xe7);

    check('TILE_LAND plane data', eq(sceneChr.slice(0, 16), [...zeros, ...ff]),
        hexArray(sceneChr.slice(0, 16)));
    check('TILE_WATER plane data', eq(sceneChr.slice(16, 32), [...ff, ...ff]),
        hexArray(sceneChr.slice(16, 32)));
    check('TILE_BANK_LEFT plane data', eq(sceneChr.slice(32, 48), [...lf, ...ff]),
        hexArray(sceneChr.slice(32, 48)));
    check('TILE_BANK_RIGHT plane data', eq(sceneChr.slice(48, 64), [...f0, ...ff]),
        hexArray(sceneChr.slice(48, 64)));
    check('TILE_BRIDGE plane data', eq(sceneChr.slice(64, 80), [...ff, ...e7]),
        hexArray(sceneChr.slice(64, 80)));

    // ---------------------------------------------------------------------
    // Nametable boot screen
    // ---------------------------------------------------------------------
    const nametable = await readBytes(0, 960, 'NesNametableRam');
    const row = r => nametable.slice(r * 32, r * 32 + 32);

    check('row 0 title', eq(row(0).slice(11, 21), textGlyphs('RIVER RAID')),
        hexArray(row(0).slice(11, 21)));
    check('row 1 subtitle', eq(row(1).slice(8, 24), textGlyphs('ATARI 800 TO NES')),
        hexArray(row(1).slice(8, 24)));
    check('row 2 phase', eq(row(2).slice(10, 22), textGlyphs('PHASE 2 BOOT')),
        hexArray(row(2).slice(10, 22)));
    check('row 4 CHR status', eq(row(4).slice(11, 21), textGlyphs('CHR RAM OK')),
        hexArray(row(4).slice(11, 21)));
    check('row 5 palette status', eq(row(5).slice(11, 21), textGlyphs('PALETTE OK')),
        hexArray(row(5).slice(11, 21)));
    check('row 6 nametable status', eq(row(6).slice(10, 22), textGlyphs('NAMETABLE OK')),
        hexArray(row(6).slice(10, 22)));
    check('row 26 FRAME label', eq(row(26).slice(11, 16), textGlyphs('FRAME')),
        hexArray(row(26).slice(11, 16)));
    check('row 28 footer', eq(row(28).slice(8, 24), textGlyphs('MESEN SMOKE TEST')),
        hexArray(row(28).slice(8, 24)));
    check('row 3 blank', row(3).every(v => v === 0), hexArray(row(3)));
    check('row 7 blank', row(7).every(v => v === 0), hexArray(row(7)));

    let sceneOk = true;
    let sceneDetail = '';
    for (let r = 0; r < 17; r++) {
        const expected = expectedRiverRow(r);
        const actual = row(8 + r);
        if (!eq(actual, expected)) {
            sceneOk = false;
            sceneDetail = `row ${8 + r}: ${hexArray(actual)}`;
            break;
        }
    }
    check('boot river scene rows 8..24', sceneOk, sceneDetail);
    check('bridge row 16 spans the water',
        row(16).slice(4, 28).every(v => v === TILE_BRIDGE),
        hexArray(row(16).slice(3, 29)));

    const digits0 = row(26).slice(17, 21);
    check('frame digits are 0..9 glyphs',
        digits0.every(v => v >= GLYPH_0 && v <= GLYPH_0 + 9),
        hexArray(digits0));

    // ---------------------------------------------------------------------
    // Handler cadence and RAM layout
    // ---------------------------------------------------------------------
    const frameAddr = symAddr('frame_counter');
    const vblAddr = symAddr('vbl_events');
    const lcdAddr = symAddr('lcd_events');
    const scrollAddr = symAddr('playfield_scroll_y');

    const frame0 = await readU16(frameAddr);
    const vbl0 = await readU16(vblAddr);
    const lcd0 = await readU16(lcdAddr);

    check('frame counter running', frame0 > 10, `frame=${frame0}`);
    check('VBL handler running', vbl0 > 10, `vbl=${vbl0}`);
    check('LCD handler runs twice per frame', Math.abs(lcd0 - 2 * vbl0) <= 2,
        `vbl=${vbl0} lcd=${lcd0}`);
    check('playfield scroll starts at 0', (await readU8(scrollAddr)) === 0);

    const cpu = await callTool('mesen_get_state', {
        component: 'cpu',
        cpuType: 'Nes',
    });
    const spMatch = cpu.match(/SP=\$([0-9A-Fa-f]{2})/);
    check('CPU state has a stack pointer', !!spMatch, cpu.trim());
    if (spMatch) {
        const sp = parseInt(spMatch[1], 16);
        check('stack stays above the attribute shadow ($01C8)', sp >= 0xc8,
            `SP=0x${sp.toString(16)}`);
    }

    // ---------------------------------------------------------------------
    // Frame digit animation and screenshot
    // ---------------------------------------------------------------------
    await sleep(1000);
    const frame1 = await readU16(frameAddr);
    const nametable1 = await readBytes(0, 960, 'NesNametableRam');
    const digits1 = nametable1.slice(26 * 32 + 17, 26 * 32 + 21);

    check('frame counter advances', frame1 - frame0 > 20 && frame1 - frame0 < 120,
        `delta=${frame1 - frame0}`);
    check('frame digits animate', !eq(digits0, digits1),
        `${digitsValue(digits0)} -> ${digitsValue(digits1)}`);

    const expected = (frame1 - 1 + 10000) % 10000;
    const displayed = digitsValue(digits1);
    const digitDiff = Math.min((expected - displayed + 10000) % 10000,
        (displayed - expected + 10000) % 10000);
    check('frame digits track the counter', digitDiff <= 2,
        `display=${displayed} counter=${frame1}`);

    fs.mkdirSync(path.dirname(SHOT), { recursive: true });
    await callTool('mesen_take_screenshot', { outputFile: SHOT });
    check('screenshot written', fs.existsSync(SHOT) && fs.statSync(SHOT).size > 1000,
        SHOT);

    // ---------------------------------------------------------------------
    // Rendered scanline geometry (catches split/scroll regressions that the
    // nametable checks above cannot see)
    // ---------------------------------------------------------------------
    const shot = readPng(SHOT);
    const isWhite = (r, g, b) => r > 200 && g > 200 && b > 200;
    const isGreen = (r, g, b) => g > 100 && r < 100 && b < 100;
    const isBlue = (r, g, b) => b > 100 && r < 100 && g < 100;
    const isBlack = (r, g, b) => r < 32 && g < 32 && b < 32;
    const whiteAt = y => countRow(shot, y, isWhite);

    check('screenshot is 256x240', shot.width === 256 && shot.height === 240,
        `${shot.width}x${shot.height}`);

    check('HUD row 0 has no scanline tear',
        rowMatches(shot, 0, isBlack) && whiteAt(1) > 20,
        `row0=${whiteAt(0)} row1=${whiteAt(1)}`);
    check('HUD text ends above the split', whiteAt(22) > 20 && rowMatches(shot, 23, isBlack),
        `row22=${whiteAt(22)} row23=${whiteAt(23)}`);

    let splitOk = true;
    let splitRow = -1;
    for (let y = 24; y < 32; y++) {
        if (!rowMatches(shot, y, isBlack)) {
            splitOk = false;
            splitRow = y;
            break;
        }
    }
    check('playfield starts at nametable row 3 (blank)', splitOk,
        splitRow >= 0 ? `white at y=${splitRow}, count=${whiteAt(splitRow)}` : '');

    check('status row 4 renders at y=32..39',
        rowMatches(shot, 32, isBlack) && whiteAt(33) > 20,
        `row32=${whiteAt(32)} row33=${whiteAt(33)}`);
    check('river row 8 starts at y=64',
        rowMatches(shot, 63, isBlack) &&
        countRow(shot, 64, isGreen) > 0 && countRow(shot, 64, isBlue) > 0,
        `y63 white=${whiteAt(63)} y64 green=${countRow(shot, 64, isGreen)} ` +
        `blue=${countRow(shot, 64, isBlue)}`);

    let bridgeOk = true;
    let bridgeRow = -1;
    for (let y = 128; y < 136; y++) {
        if (whiteAt(y) === 0) {
            bridgeOk = false;
            bridgeRow = y;
            break;
        }
    }
    check('bridge row 16 renders at y=128..135',
        bridgeOk && whiteAt(127) === 0 && whiteAt(136) === 0,
        bridgeRow >= 0 ? `no white at y=${bridgeRow}` : `above=${whiteAt(127)} below=${whiteAt(136)}`);

    check('footer row 28 renders at y=224..231',
        rowMatches(shot, 224, isBlack) && whiteAt(225) > 20,
        `row224=${whiteAt(224)} row225=${whiteAt(225)}`);

    if (failures) {
        console.log(`NES smoke FAILED (${failures})`);
        process.exitCode = 1;
    } else {
        console.log('NES smoke passed');
    }
}

main().catch(err => {
    console.error(err.stack || err.message);
    process.exitCode = 1;
});
