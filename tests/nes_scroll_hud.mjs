/*
 * Phase 3 scroll + HUD regression (MesenCE-MCP, Streamable HTTP).
 *
 * Start Mesen before running:
 *   .\MesenCE-MCP\MesenCE-MCP-Windows.exe --mcp --mcp-port=9100 \
 *       --enableStdout --doNotSaveSettings --nes.region=Ntsc
 *
 *   node tests/nes_scroll_hud.mjs
 *
 * Verifies:
 *   - the playfield scroll advances one pixel per frame and the HUD stays
 *     fixed for 120+ frames,
 *   - the sprite-0 split lands exactly on scanline 24 with no seam,
 *   - a frozen scroll + scroll+1 pair shifts the whole playfield by exactly
 *     one pixel (A[y+1] == B[y]),
 *   - the dynamic HUD fields (score, fuel, lives, bridge, icons) update.
 */
import fs from 'node:fs';
import path from 'node:path';

import {
    init,
    callTool,
    readBytes,
    readU8,
    readU16,
    writeBytes,
    symbolsFromNoi,
    requireSymbol,
    sleep,
} from './mcp_client.mjs';
import { readPng } from './png.mjs';

const ROM = path.resolve('roms/river_raid.nes');
const NOI = path.resolve('roms/river_raid.noi');
const SHOT_A = path.resolve('build/scroll_a.png');
const SHOT_B = path.resolve('build/scroll_b.png');
const SHOT_HUD = path.resolve('build/hud_dynamic.png');

const TILE_GAUGE_BL = 218;
const TILE_GAUGE_B = 220;
const TILE_GAUGE_BC = 222;
const TILE_GAUGE_BR = 224;
const TILE_DIGIT_YELLOW = 246;
const GLYPH_0 = 80;
const HUD_FUEL_BAR_SLOT = 54;
const HUD_FUEL_BAR_SLOT2 = 55;
const HUD_LIFE_SLOT = 44;

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

function isGreen(r, g, b) {
    return g > 90 && r < 80 && b < 80;
}

function isBlue(r, g, b) {
    return b > 90 && r < 80 && g < 80;
}

function firstPlayfieldRow(image) {
    for (let y = 30; y < 50; y++) {
        let n = 0;
        for (let x = 0; x < 256; x++) {
            const [r, g, b] = image.pixel(x, y);
            if (isGreen(r, g, b) || isBlue(r, g, b)) n++;
        }
        if (n > 32) return y;
    }
    return -1;
}

function hudHash(image) {
    let hash = 2166136261;
    for (let y = 0; y < 36; y++) {
        for (let x = 0; x < 256; x++) {
            const [r, g, b] = image.pixel(x, y);
            hash ^= r;
            hash = Math.imul(hash, 16777619);
            hash ^= g;
            hash = Math.imul(hash, 16777619);
            hash ^= b;
            hash = Math.imul(hash, 16777619);
        }
    }
    return hash >>> 0;
}

async function screenshot(file) {
    await callTool('mesen_take_screenshot', { outputFile: file });
    return readPng(file);
}

async function main() {
    if (!fs.existsSync(ROM)) throw new Error(`Missing ${ROM}`);
    if (!fs.existsSync(NOI)) throw new Error(`Missing ${NOI}`);

    const sym = symbolsFromNoi(NOI);
    const symAddr = name => requireSymbol(sym, name);

    await init();
    await callTool('mesen_load_rom', { filepath: ROM });
    await sleep(900);
    await callTool('mesen_reset', { type: 'hard' });
    await sleep(1400);
    try {
        await callTool('mesen_resume_execution', {});
    } catch {
        /* already running */
    }

    const frameAddr = symAddr('frame_counter');
    const scrollAddr = symAddr('playfield_scroll_y');
    const freezeAddr = symAddr('river_freeze');
    const vblAddr = symAddr('vbl_events');
    const splitAddr = symAddr('split_events');

    // ---------------------------------------------------------------------
    // 1. Free-running scroll: one pixel per frame, split once per frame
    // ---------------------------------------------------------------------
    const frame0 = await readU16(frameAddr);
    const scroll0 = await readU8(scrollAddr);
    const vbl0 = await readU16(vblAddr);
    const split0 = await readU16(splitAddr);

    await sleep(2000);

    const frame1 = await readU16(frameAddr);
    const scroll1 = await readU8(scrollAddr);
    const vbl1 = await readU16(vblAddr);
    const split1 = await readU16(splitAddr);

    const frameDelta = frame1 - frame0;
    const scrollDelta = (scroll1 - scroll0 + 240) % 240;
    check('playfield scroll advances 1 pixel per frame',
        Math.abs(scrollDelta - frameDelta) <= 3,
        `frames=${frameDelta} scroll=${scrollDelta}`);
    check('sprite-0 split runs once per frame',
        Math.abs((split1 - split0) - (vbl1 - vbl0)) <= 2,
        `vbl=${vbl1 - vbl0} split=${split1 - split0}`);

    // ---------------------------------------------------------------------
    // 2. HUD stays fixed while the playfield scrolls
    // ---------------------------------------------------------------------
    const hashes = [];
    for (let i = 0; i < 12; i++) {
        await sleep(120);
        const img = await screenshot(SHOT_HUD);
        hashes.push(hudHash(img));
    }
    check('top 36 scanlines stay identical over 120+ frames',
        hashes.every(h => h === hashes[0]),
        `${new Set(hashes).size} unique hash(es)`);

    const running = await screenshot(SHOT_HUD);
    let hudLit = 0;
    for (let y = 0; y < 36; y++) {
        for (let x = 0; x < 256; x++) {
            const [r, g, b] = running.pixel(x, y);
            if (r > 100 || g > 100 || b > 100) hudLit++;
        }
    }
    check('top 36 scanlines contain HUD text', hudLit > 200, `lit=${hudLit}`);

    // ---------------------------------------------------------------------
    // 3. Exact 1-pixel shift: freeze the scroll, capture 100 and 101
    // ---------------------------------------------------------------------
    await callTool('mesen_playback', { action: 'pause' });
    await sleep(150);
    await writeBytes(freezeAddr, [1]);
    await writeBytes(scrollAddr, [100]);
    await callTool('mesen_playback', { action: 'resume' });
    await sleep(300);
    await callTool('mesen_playback', { action: 'pause' });
    await sleep(150);
    await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
    const a = await screenshot(SHOT_A);

    await writeBytes(scrollAddr, [101]);
    await callTool('mesen_playback', { action: 'resume' });
    await sleep(300);
    await callTool('mesen_playback', { action: 'pause' });
    await sleep(150);
    await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
    const b = await screenshot(SHOT_B);

    let hudDiff = 0;
    for (let y = 0; y < 36; y++) {
        for (let x = 0; x < 256; x++) {
            const pa = a.pixel(x, y);
            const pb = b.pixel(x, y);
            if (pa[0] !== pb[0] || pa[1] !== pb[1] || pa[2] !== pb[2]) hudDiff++;
        }
    }
    check('HUD is unchanged between scroll 100 and 101', hudDiff === 0,
        `diff=${hudDiff}`);

    let shifted = 0;
    let shiftDiff = 0;
    for (let y = 39; y < 239; y++) {
        for (let x = 0; x < 256; x++) {
            const pa = a.pixel(x, y + 1);
            const pb = b.pixel(x, y);
            if (pa[0] === pb[0] && pa[1] === pb[1] && pa[2] === pb[2]) shifted++;
            else shiftDiff++;
        }
    }
    check('river advances by exactly one pixel per scroll step',
        shiftDiff === 0, `matched=${shifted} diff=${shiftDiff}`);

    check('split is on scanline 36/37 for scroll 100',
        firstPlayfieldRow(a) === 36 || firstPlayfieldRow(a) === 37,
        `first=${firstPlayfieldRow(a)}`);
    check('split is on scanline 36/37 for scroll 101',
        firstPlayfieldRow(b) === 36 || firstPlayfieldRow(b) === 37,
        `first=${firstPlayfieldRow(b)}`);

    // ---------------------------------------------------------------------
    // 4. Dynamic HUD fields
    // ---------------------------------------------------------------------
    await writeBytes(freezeAddr, [0]);
    await writeBytes(scrollAddr, [0]);
    await callTool('mesen_playback', { action: 'resume' });
    await sleep(200);

    await callTool('mesen_playback', { action: 'pause' });
    await sleep(150);
    await writeBytes(symAddr('hud_score_bcd'), [0x12, 0x34, 0x56]);
    await writeBytes(symAddr('hud_fuel'), [0]);
    await writeBytes(symAddr('hud_lives'), [2]);
    await writeBytes(symAddr('hud_bridge'), [3]);
    await callTool('mesen_playback', { action: 'resume' });
    await sleep(300);
    await callTool('mesen_playback', { action: 'pause' });
    await sleep(150);

    const nt0 = await readBytes(0x2000, 128, 'NesPpuMemory');
    const row1 = nt0.slice(32, 64);
    const row2 = nt0.slice(64, 96);
    const row3 = nt0.slice(96, 128);

    check('score digits update to 123456',
        eq(row1.slice(11, 17), [1, 2, 3, 4, 5, 6].map(d => TILE_DIGIT_YELLOW + d)),
        hexArray(row1.slice(11, 17)));
    check('high score stays a single black 0',
        eq(row1.slice(25, 31), [0, 0, 0, 0, 0, GLYPH_0]),
        hexArray(row1.slice(25, 31)));
    check('bridge digit updates to 3',
        row2[29] === 0 && row2[30] === TILE_DIGIT_YELLOW + 3,
        hexArray(row2.slice(29, 31)));
    check('game row keeps the gauge floor',
        eq(row3.slice(9, 19), [
            TILE_GAUGE_BL, TILE_GAUGE_BL + 1,
            TILE_GAUGE_B, TILE_GAUGE_B + 1,
            TILE_GAUGE_BC, TILE_GAUGE_BC + 1,
            TILE_GAUGE_B, TILE_GAUGE_B + 1,
            TILE_GAUGE_BR, TILE_GAUGE_BR + 1,
        ]), hexArray(row3.slice(9, 19)));

    const oam = symAddr('shadow_OAM');
    const slot = await readBytes(oam + HUD_FUEL_BAR_SLOT * 4, 4, 'NesMemory');
    const slot2 = await readBytes(oam + HUD_FUEL_BAR_SLOT2 * 4, 4, 'NesMemory');
    check('empty fuel moves both bar halves to the E side',
        slot[3] === 80 && slot2[3] === 80,
        `bar x=${slot[3]}/${slot2[3]}`);

    const life0 = await readBytes(oam + HUD_LIFE_SLOT * 4, 4, 'NesMemory');
    const life1 = await readBytes(oam + (HUD_LIFE_SLOT + 1) * 4, 4, 'NesMemory');
    const life2 = await readBytes(oam + (HUD_LIFE_SLOT + 2) * 4, 4, 'NesMemory');
    check('two lives icons remain visible',
        life0[3] === 160 && life0[0] === 23 &&
        life1[3] === 168 && life1[0] === 23 &&
        life2[0] === 240,
        `x=${life0[3]},${life1[3]} y2=${life2[0]}`);

    await callTool('mesen_playback', { action: 'resume' });

    if (failures) {
        console.log(`scroll/HUD test FAILED (${failures})`);
        process.exitCode = 1;
    } else {
        console.log('scroll/HUD test passed');
    }
}

main().catch(err => {
    console.error(err.stack || err.message);
    process.exitCode = 1;
});
