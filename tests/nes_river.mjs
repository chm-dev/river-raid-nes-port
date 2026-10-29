/*
 * Phase 4 procedural river regression (MesenCE-MCP, Streamable HTTP).
 *
 * Start Mesen before running:
 *   .\MesenCE-MCP\MesenCE-MCP-Windows.exe --mcp --mcp-port=9100 \
 *       --enableStdout --doNotSaveSettings --nes.region=Ntsc
 *
 *   node tests/nes_river.mjs
 *
 * Drives the ROM one PPU frame at a time, stopping exactly when each new
 * 8-row tile row has been streamed.  It compares the last raw 48-byte row,
 * the generator state and the collision ring against the golden stream
 * produced by tools/river_model.py (tests/data/river_golden.json).
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

const ROM = path.resolve('roms/river_raid.nes');
const NOI = path.resolve('roms/river_raid.noi');
const GOLDEN = path.resolve('tests/data/river_golden.json');
const TILE_ROWS = 400;

let failures = 0;

function check(name, ok, detail = '') {
    console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
    if (!ok) failures++;
}

function eq(a, b) {
    return a.length === b.length && a.every((v, i) => v === b[i]);
}

function hex(a) {
    return a.map(v => v.toString(16).padStart(2, '0')).join('');
}

async function stepFrame() {
    await callTool('mesen_step', { cpuType: 'Nes', count: 1, stepType: 'PpuFrame' });
}

async function main() {
    if (!fs.existsSync(GOLDEN)) {
        throw new Error(`Missing ${GOLDEN}; run python tools/gen_river_golden.py`);
    }
    const golden = JSON.parse(fs.readFileSync(GOLDEN, 'utf8'));

    const sym = symbolsFromNoi(NOI);
    const symAddr = name => requireSymbol(sym, name);

    await init();
    await callTool('mesen_load_rom', { filepath: ROM });
    await sleep(900);
    await callTool('mesen_reset', { type: 'hard' });
    await sleep(200);
    try {
        await callTool('mesen_resume_execution', {});
    } catch {
        /* already running */
    }
    await sleep(1400);
    await callTool('mesen_playback', { action: 'pause' });
    await sleep(100);

    const scrollAddr = symAddr('playfield_scroll_y');
    const freezeAddr = symAddr('river_freeze');
    const distAddr = symAddr('river_distance');
    const rowAddr = symAddr('river_debug_row');
    const stateAddr = symAddr('river_debug_state');
    const leftAddr = symAddr('river_left_x');
    const rightAddr = symAddr('river_right_x');
    const ilAddr = symAddr('river_island_left_x');
    const irAddr = symAddr('river_island_right_x');
    const flagsAddr = symAddr('river_flags');

    await writeBytes(freezeAddr, [0]);

    // The free-running boot may already have generated a few tile rows;
    // align the golden index with the live counter.
    let start = await readU16(distAddr);
    if (start > TILE_ROWS - 8) {
        start = 0; // should not happen on a fresh reset
    }
    const fcAddr = symAddr('frame_counter');
    console.log(`boot: frame=${await readU16(fcAddr)} scroll=${await readU8(scrollAddr)} ` +
        `freeze=${await readU8(freezeAddr)} distance=${start}`);

    let okRows = 0;
    let okCollision = 0;
    let samples = 0;
    let firstMatch = -1;
    let firstRowFail = null;
    let firstCollisionFail = null;
    const seen = new Set();

    // The game runs freely; pause it and identify the debug row by matching
    // the 12-byte generator state against the golden stream.  This avoids
    // racing the distance counter and works even if a block is in flight.
    for (let iter = 0; iter < TILE_ROWS * 8 && samples < TILE_ROWS; iter++) {
        await callTool('mesen_playback', { action: 'pause' });
        await sleep(10);
        const state = await readBytes(stateAddr, 12);
        let match = -1;
        for (let k = 0; k < golden.length; k++) {
            if (eq(state, golden[k].state)) {
                match = k;
                break;
            }
        }
        if (match >= 0 && !seen.has(match)) {
            if (firstMatch < 0) firstMatch = match;
            seen.add(match);
            samples++;
            const g = golden[match];
            const row = await readBytes(rowAddr, 48);
            const slot = g.slot;
            const collision = {
                left: await readU8(leftAddr + slot),
                right: await readU8(rightAddr + slot),
                il: await readU8(ilAddr + slot),
                ir: await readU8(irAddr + slot),
                flags: await readU8(flagsAddr + slot),
            };
            if (eq(row, Buffer.from(g.row, 'hex'))) okRows++;
            else if (firstRowFail === null) {
                const want = Buffer.from(g.row, 'hex');
                let diff = -1;
                for (let k = 0; k < Math.min(row.length, want.length); k++) {
                    if (row[k] !== want[k]) { diff = k; break; }
                }
                firstRowFail = `tile row ${match}: len=${row.length}/${want.length} ` +
                    `firstdiff=${diff} got=${diff >= 0 ? row[diff].toString(16) : '-'} ` +
                    `want=${diff >= 0 ? want[diff].toString(16) : '-'}`;
            }
            const want = g.collision;
            if (collision.left === want.left && collision.right === want.right &&
                collision.il === want.il && collision.ir === want.ir &&
                collision.flags === want.flags) {
                okCollision++;
            } else if (firstCollisionFail === null) {
                firstCollisionFail = `tile row ${match}: got ${JSON.stringify(collision)} want ${JSON.stringify(want)}`;
            }
        }
        await callTool('mesen_playback', { action: 'resume' });
        await sleep(60);
    }

    // The ROM is already running when the test first attaches, so the first
    // few tile rows can complete before the first pause.  Measure coverage
    // from the first state actually observed; every later row must be seen.
    const expectedCoverage = TILE_ROWS - Math.max(0, firstMatch);
    const expected = samples;
    check('sampled every row from the first observed state',
        samples === expectedCoverage,
        `${samples}/${expectedCoverage} (first matched row ${firstMatch})`);
    check('procedural rows match the Atari model', okRows === expected,
        `${okRows}/${expected}${firstRowFail ? '  ' + firstRowFail : ''}`);
    check('collision ring matches the Atari model', okCollision === expected,
        `${okCollision}/${expected}${firstCollisionFail ? '  ' + firstCollisionFail : ''}`);

    if (failures) {
        console.log(`river test FAILED (${failures})`);
        process.exit(1);
    }
    console.log('river test passed');
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
