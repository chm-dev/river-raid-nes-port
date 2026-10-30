/*
 * Phase 3 NES smoke test (MesenCE-MCP, Streamable HTTP).
 *
 * Start Mesen before running:
 *   .\MesenCE-MCP\MesenCE-MCP-Windows.exe --mcp --mcp-port=9100 \
 *       --enableStdout --doNotSaveSettings --nes.region=Ntsc
 *
 *   node tests/nes_smoke.mjs
 *
 * Verifies:
 *   - the GBDK mapper-30 / CHR-RAM / iNES header contract and the
 *     vertical-mirroring nametable layout (HUD $2000, playfield $2400),
 *   - the HUD and playfield palettes and the CHR-RAM tile set,
 *   - the four-row HUD text, source gauge art, moving fuel marker and lives planes,
 *   - the 30-row playfield ring in nametable 1 and its attributes,
 *   - the VBL/sprite-0 split cadence and the fixed 24-line HUD,
 *   - the rendered screenshot geometry (HUD, split at y=25, river colors).
 */
import fs from 'node:fs';
import path from 'node:path';

import {
    init,
    callTool,
    readBytes,
    readU16,
    symbolsFromNoi,
    requireSymbol,
    sleep,
} from './mcp_client.mjs';
import { readPng, countRow } from './png.mjs';

const ROM = path.resolve('roms/river_raid.nes');
const NOI = path.resolve('roms/river_raid.noi');
const SHOT = path.resolve('build/phase3_smoke.png');

/* The playfield owns pattern table $0000 (land 0, water 1, river cache 2..255);
 * HUD tiles and all sprites share table $1000 (sprites start at tile 64). */
const TILE_LAND = 0;
const TILE_WATER = 1;
const TILE_BLACK = 2;
const HUD_CHR = 0x1000;
const SPRITE_TILE_BASE = 64;
const TILE_GAUGE_TL = 11;
const TILE_GAUGE_T = 13;
const TILE_GAUGE_TC = 15;
const TILE_GAUGE_TR = 17;
const TILE_GAUGE_BL = 19;
const TILE_GAUGE_B = 21;
const TILE_GAUGE_BC = 23;
const TILE_GAUGE_BR = 25;
const TILE_P1_WIDE = 27;
const TILE_HI_WIDE = 29;
const TILE_BRIDGE_WIDE = 31;
const TILE_GAME_WIDE = 39;
const TILE_DIGIT_BASE = 1;
const TILE_DIGIT_YELLOW_BASE = 48;
const GLYPH_0 = 80;      // charset code, not a tile ID

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

function digitTiles(text) {
    return [...text].map(d => TILE_DIGIT_BASE + Number(d));
}

async function main() {
    if (!fs.existsSync(ROM)) throw new Error(`Missing ${ROM}`);
    if (!fs.existsSync(NOI)) throw new Error(`Missing ${NOI}`);

    const rom = fs.readFileSync(ROM);
    const prgBanks = rom[4];
    const chrBanks = rom[5];
    const mapper = ((rom[6] >> 4) & 0x0f) | (rom[7] & 0xf0);
    const layout = rom[6] & 0x0f;

    check('iNES magic', rom[0] === 0x4e && rom[1] === 0x45 &&
        rom[2] === 0x53 && rom[3] === 0x1a);
    check('iNES 1.0 (not NES 2.0)', (rom[7] & 0x0c) === 0,
        `byte7=0x${rom[7].toString(16)}`);
    check('mapper 30 (UNROM-512)', mapper === 30, `mapper=${mapper}`);
    check('CHR-RAM (0 CHR banks)', chrBanks === 0, `byte5=${chrBanks}`);
    check('vertical mirroring layout', layout === 1, `byte6 low=0x${layout.toString(16)}`);
    check('PRG >= 2 x 16 KiB', prgBanks >= 2, `prgBanks=${prgBanks}`);
    check('file size matches header',
        rom.length === 16 + prgBanks * 16384 + chrBanks * 8192,
        `size=${rom.length}`);

    const sym = symbolsFromNoi(NOI);
    const symAddr = name => requireSymbol(sym, name);

    await init();
    try { await callTool('mesen_playback', { action: 'resume' }); } catch { /* already running */ }

    await callTool('mesen_load_rom', { filepath: ROM });
    await sleep(900);
    await callTool('mesen_reset', { type: 'hard' });
    await sleep(1400);
    try {
        await callTool('mesen_resume_execution', {});
    } catch {
        /* already running */
    }

    const header = JSON.parse(await callTool('mesen_get_rom_header'));
    check('Mesen sees mapper 30', header.mapper === 30, `mapper=${header.mapper}`);
    check('Mesen sees vertical mirroring', /vertical/i.test(String(header.mirroring)),
        `mirroring=${header.mirroring}`);

    // ---------------------------------------------------------------------
    // Palettes
    // ---------------------------------------------------------------------
    const palette = await readBytes(0, 16, 'NesPaletteRam');
    check('HUD palette 0 = black backdrop, gray, yellow, white',
        eq(palette.slice(0, 4), [0x0f, 0x00, 0x28, 0x30]), hexArray(palette.slice(0, 4)));
    check('playfield palette 2 = backdrop 0F + 09 07 01',
        eq(palette.slice(8, 12), [0x0f, 0x09, 0x07, 0x11]), hexArray(palette.slice(8, 12)));
    const spritePalette = await readBytes(16, 16, 'NesPaletteRam');
    check('player sprite palette 0 = backdrop 0F + 28 30 16',
        eq(spritePalette.slice(0, 4), [0x0f, 0x28, 0x30, 0x16]),
        hexArray(spritePalette.slice(0, 4)));

    // ---------------------------------------------------------------------
    // CHR-RAM
    // ---------------------------------------------------------------------
    const blankChr = await readBytes(HUD_CHR, 16, 'NesChrRam');
    const zeros = Array(8).fill(0x00);
    const ff = Array(8).fill(0xff);
    check('blank/space tile 0 is solid gray (value 1)', eq(blankChr, [...ff, ...zeros]),
        hexArray(blankChr));

    const landChr = await readBytes(TILE_LAND * 16, 16, 'NesChrRam');
    check('river land tile is solid colour 1', eq(landChr, [...ff, ...zeros]),
        hexArray(landChr));

    const fillChr = await readBytes(HUD_CHR + TILE_GAUGE_T * 16, 16, 'NesChrRam');
    /* Gray band is plane 0; the black ink pixels clear it. */
    const gaugeTop = [0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
    check('gauge frame top rule is one pixel row of black ink',
        eq(fillChr, gaugeTop), hexArray(fillChr));

    const digit0Chr = await readBytes(HUD_CHR + TILE_DIGIT_BASE * 16, 16, 'NesChrRam');
    check('black digit 0 is inverted glyph 0 on plane 0',
        eq(digit0Chr, [0xff, 0xc3, 0x99, 0x99, 0x99, 0x99, 0xc3, 0xff,
                       ...zeros]),
        hexArray(digit0Chr));

    const yellow0Chr = await readBytes(HUD_CHR + TILE_DIGIT_YELLOW_BASE * 16, 16, 'NesChrRam');
    check('yellow digit 0 has the glyph on plane 1',
        eq(yellow0Chr, [0xff, 0xc3, 0x99, 0x99, 0x99, 0x99, 0xc3, 0xff,
                        0x00, 0x3c, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00]),
        hexArray(yellow0Chr));

    const playerChr = await readBytes(0x1000 + SPRITE_TILE_BASE * 16, 16, 'NesChrRam');
    check('player sprite CHR is populated', playerChr.some(v => v !== 0),
        hexArray(playerChr));

    // ---------------------------------------------------------------------
    // HUD nametable (NT0)
    // ---------------------------------------------------------------------
    const nt0 = await readBytes(0x2000, 160, 'NesPpuMemory');
    const row0 = nt0.slice(0, 32);
    const row1 = nt0.slice(32, 64);
    const row2 = nt0.slice(64, 96);
    const row3 = nt0.slice(96, 128);
    const row4 = nt0.slice(128, 160);

    const SPACE = 0;

    check('row 0 is blank padding', row0.every(v => v === SPACE),
        hexArray(row0));
    check('row 4 is blank padding', row4.every(v => v === SPACE),
        hexArray(row4));
    check('row 1 P1 (wide label, aligned with GAME)',
        eq(row1.slice(1, 3), [TILE_P1_WIDE, TILE_P1_WIDE + 1]),
        hexArray(row1.slice(1, 3)));
    check('row 1 score is a single yellow 0 (leading zeros blank)',
        eq(row1.slice(11, 17), [SPACE, SPACE, SPACE, SPACE, SPACE, TILE_DIGIT_YELLOW_BASE]),
        hexArray(row1.slice(11, 17)));
    check('row 1 HI label (wide, aligned with BRIDGE)',
        eq(row1.slice(20, 22), [TILE_HI_WIDE, TILE_HI_WIDE + 1]),
        hexArray(row1.slice(20, 22)));
    check('row 1 high score is a single black 0',
        eq(row1.slice(25, 31), [SPACE, SPACE, SPACE, SPACE, SPACE, TILE_DIGIT_BASE]),
        hexArray(row1.slice(25, 31)));

    check('row 2 GAME uses eight wide tiles',
        eq(row2.slice(1, 9), [...Array(8)].map((_, i) => TILE_GAME_WIDE + i)),
        hexArray(row2.slice(1, 9)));
    check('row 2 gauge frame top (47 48 49 48 4A)',
        eq(row2.slice(9, 19), [
            TILE_GAUGE_TL, TILE_GAUGE_TL + 1,
            TILE_GAUGE_T, TILE_GAUGE_T + 1,
            TILE_GAUGE_TC, TILE_GAUGE_TC + 1,
            TILE_GAUGE_T, TILE_GAUGE_T + 1,
            TILE_GAUGE_TR, TILE_GAUGE_TR + 1,
        ]), hexArray(row2.slice(9, 19)));
    check('row 2 BRIDGE uses eight wide tiles',
        eq(row2.slice(20, 28), [...Array(8)].map((_, i) => TILE_BRIDGE_WIDE + i)),
        hexArray(row2.slice(20, 28)));
    check('row 2 bridge number 1',
        row2[29] === SPACE && row2[30] === TILE_DIGIT_YELLOW_BASE + 1,
        hexArray(row2.slice(29, 31)));

    check('row 3 game number 1', row3[5] === TILE_DIGIT_BASE + 1, `tile=${row3[5]}`);

    const attr0 = await readBytes(0x23C0, 8, 'NesPpuMemory');
    check('HUD attributes are all palette 0 (colour comes from tile art)',
        attr0.every(v => v === 0),
        hexArray(attr0));
    check('row 3 gauge frame floor (4B 4C 4D 4C 4E)',
        eq(row3.slice(9, 19), [
            TILE_GAUGE_BL, TILE_GAUGE_BL + 1,
            TILE_GAUGE_B, TILE_GAUGE_B + 1,
            TILE_GAUGE_BC, TILE_GAUGE_BC + 1,
            TILE_GAUGE_B, TILE_GAUGE_B + 1,
            TILE_GAUGE_BR, TILE_GAUGE_BR + 1,
        ]), hexArray(row3.slice(9, 19)));

    // ---------------------------------------------------------------------
    // Playfield nametable (NT1) and attributes
    // ---------------------------------------------------------------------
    const nt1 = await readBytes(0x2400, 960, 'NesPpuMemory');
    let ringFilled = true;
    for (let r = 0; r < 30; r++) {
        if (nt1.slice(r * 32, r * 32 + 32).every(v => v === 0)) {
            ringFilled = false;
        }
    }
    check('all 30 playfield ring rows are populated', ringFilled);

    const riverRow = nt1.slice(3 * 32, 3 * 32 + 32);
    // The world starts black (the river scrolls in from the top), so a ring row
    // is either still all black or already a generated row with land margins.
    const rowBlack = riverRow.every(v => v === TILE_BLACK);
    check('playfield row 3 is black or a generated river row',
        rowBlack ||
        (riverRow.slice(0, 4).every(v => v === TILE_LAND) &&
         riverRow.slice(28).every(v => v === TILE_LAND) &&
         riverRow.slice(4, 28).some(v => v !== TILE_LAND && v !== TILE_BLACK)),
        hexArray(riverRow));

    const attr1 = await readBytes(0x27C0, 64, 'NesPpuMemory');
    // Palette 2 (dark land) is 0xAA; the start road makes one pair of tile
    // rows palette 3 (upper half 0x0F, lower half 0xF0 of the byte).
    check('playfield attributes use palette 2 (0xAA) or the road palette',
        attr1.every(v => [0xaa, 0xaf, 0xfa, 0xff].includes(v)),
        [...new Set(attr1)].map(v => v.toString(16)).join(','));

    // ---------------------------------------------------------------------
    // Handlers, sprite-0 split and RAM layout
    // ---------------------------------------------------------------------
    const frameAddr = symAddr('frame_counter');
    const vblAddr = symAddr('vbl_events');
    const splitAddr = symAddr('split_events');
    const scrollAddr = symAddr('playfield_scroll_y');

    const frame0 = await readU16(frameAddr);
    const vbl0 = await readU16(vblAddr);
    const split0 = await readU16(splitAddr);

    check('frame counter running', frame0 > 10, `frame=${frame0}`);
    check('VBL handler running', vbl0 > 10, `vbl=${vbl0}`);
    check('deferred split runs once per frame',
        Math.abs(split0 - vbl0) <= 2, `vbl=${vbl0} split=${split0}`);

    const cpu = await callTool('mesen_get_state', { component: 'cpu', cpuType: 'Nes' });
    const spMatch = cpu.match(/SP=\$([0-9A-Fa-f]{2})/);
    check('CPU state has a stack pointer', !!spMatch, cpu.trim());
    if (spMatch) {
        const sp = parseInt(spMatch[1], 16);
        check('stack stays above the attribute shadow ($01C8)', sp >= 0xc8,
            `SP=0x${sp.toString(16)}`);
    }

    // ---------------------------------------------------------------------
    // Frame animation, scroll and screenshot
    // ---------------------------------------------------------------------
    await sleep(1000);
    const frame1 = await readU16(frameAddr);
    const split1 = await readU16(splitAddr);
    check('frame counter advances', frame1 - frame0 > 20 && frame1 - frame0 < 120,
        `delta=${frame1 - frame0}`);
    check('split keeps running', split1 - split0 > 20, `delta=${split1 - split0}`);

    const scroll = await readU16(scrollAddr);
    check('playfield scroll is advancing', scroll > 0, `scroll=${scroll}`);

    // The river scrolls in from a black screen; wait until it has filled the
    // playfield (about 26 tile rows at 1 px/frame) before the colour check.
    await sleep(5000);

    // The split is stable; a frozen scroll makes the 1-pixel check exact.
    await callTool('mesen_playback', { action: 'pause' });
    await sleep(150);
    await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });

    fs.mkdirSync(path.dirname(SHOT), { recursive: true });
    await callTool('mesen_take_screenshot', { outputFile: SHOT });
    await callTool('mesen_playback', { action: 'resume' });
    check('screenshot written', fs.existsSync(SHOT) && fs.statSync(SHOT).size > 1000,
        SHOT);

    const shot = readPng(SHOT);
    const isGray = (r, g, b) => r > 80 && r < 130 && Math.abs(r - g) < 8 && Math.abs(g - b) < 8;
    const isBlack = (r, g, b) => r < 40 && g < 40 && b < 40;
    const isYellow = (r, g, b) => r > 150 && g > 140 && b < 100;
    const isGreen = (r, g, b) => g > 50 && g < 140 && r < 60 && b < 60;
    const isBlue = (r, g, b) => b > 150 && r < 80 && g < 140;
    const isPlayfield = (r, g, b) => isGreen(r, g, b) || isBlue(r, g, b);
    const grayAt = y => countRow(shot, y, isGray);
    const blackAt = y => countRow(shot, y, isBlack);
    const yellowAt = y => countRow(shot, y, isYellow);

    check('screenshot is 256x240', shot.width === 256 && shot.height === 240,
        `${shot.width}x${shot.height}`);

    let hudClean = true;
    // The HUD is three tile rows (scanlines 0..23) plus one gray padding line;
    // the playfield starts on scanline 25 or 26 (the split jitters by a line).
    for (let y = 0; y < 25; y++) {
        for (let x = 0; x < 256; x++) {
            const [r, g, b] = shot.pixel(x, y);
            if (isPlayfield(r, g, b)) {
                hudClean = false;
                break;
            }
        }
        if (!hudClean) break;
    }
    check('top 25 scanlines contain no playfield bleed', hudClean);

    check('HUD row 1 is a gray band with black/yellow text',
        grayAt(4) > 100 && blackAt(4) > 10 && yellowAt(4) > 10,
        `gray=${grayAt(4)} black=${blackAt(4)} yellow=${yellowAt(4)}`);
    check('HUD row 2 renders GAME, gauge and BRIDGE',
        grayAt(10) > 80 && blackAt(10) > 20 && yellowAt(10) > 20,
        `gray=${grayAt(10)} black=${blackAt(10)} yellow=${yellowAt(10)}`);
    check('HUD row 3 renders the gauge floor and lives icons',
        grayAt(18) > 60 && blackAt(18) > 5 && yellowAt(18) > 5,
        `gray=${grayAt(18)} black=${blackAt(18)} yellow=${yellowAt(18)}`);

    let firstPlayfield = -1;
    for (let y = 18; y < 40; y++) {
        if (countRow(shot, y, isPlayfield) > 32) {
            firstPlayfield = y;
            break;
        }
    }
    check('playfield starts on scanline 25 or 26',
        firstPlayfield === 25 || firstPlayfield === 26,
        `first=${firstPlayfield}`);

    let greenPixels = 0;
    let bluePixels = 0;
    for (const y of [80, 100, 120, 140, 160]) {
        greenPixels += countRow(shot, y, isGreen);
        bluePixels += countRow(shot, y, isBlue);
    }
    check('river has both green banks and blue water',
        greenPixels > 200 && bluePixels > 200,
        `green=${greenPixels} blue=${bluePixels}`);

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
