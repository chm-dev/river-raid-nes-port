/* High-score save in cartridge SRAM (bank 3, same scheme as the robbo port's
 * save.c).  Header: MBC5+RAM+BATTERY ($1B), 8 KB RAM ($149 = 2).
 *
 * Record (20 bytes), two copies at SRAM $0100 and $0120 (never the window base:
 * some FRAM boards do not store there):
 *   0..3  'R','R','H','S'
 *   4     format version
 *   5     sequence number (the copy with the newer sequence wins)
 *   6..17 four 3-byte BCD hi-scores, one per start level (pl_hi layout)
 *   18    checksum (xor of 5..17 ^ seed)
 *   19    $A5 marker
 * A store rewrites the stale copy: magic cleared first, restored last, then read
 * back and retried, so a power cut mid-write leaves the other copy intact.
 * Blank SRAM (zeros, $FF or random) fails magic/marker/checksum and loads as 0.
 */
#pragma bank 3

#include <gb/gb.h>
#include <stdint.h>
#include "../save.h"

#define SAVE_RAM        ((volatile uint8_t *)0xA000)
#define SAVE_SLOT_A     0x0100u
#define SAVE_SLOT_B     0x0120u
#define SAVE_VERSION    1u
#define SAVE_MARKER     0xA5u
#define SAVE_SEED       0x3Cu
#define SAVE_DATA_LEN   12u
#define SAVE_TRIES      3u

static const uint8_t save_magic[4] = { 'R', 'R', 'H', 'S' };

static uint8_t save_last[SAVE_DATA_LEN];    /* what the cartridge holds now */
static uint8_t save_seq;                    /* sequence of the newest valid copy */
static uint8_t sv_i;
static uint8_t sv_sum;
static uint8_t sv_tries;

/* Checksum of the sequence byte and payload at `rec`. */
static uint8_t save_sum(volatile uint8_t *rec) {
    sv_sum = SAVE_SEED;
    for (sv_i = 5u; sv_i < 18u; sv_i++) sv_sum ^= rec[sv_i];
    return sv_sum;
}

/* 1 when the copy at `ofs` is valid (RAM must be enabled). */
static uint8_t save_valid(uint16_t ofs) {
    volatile uint8_t *rec = SAVE_RAM + ofs;
    if (rec[0] != save_magic[0] || rec[1] != save_magic[1] ||
        rec[2] != save_magic[2] || rec[3] != save_magic[3] ||
        rec[4] != SAVE_VERSION || rec[19] != SAVE_MARKER) return 0;
    return (uint8_t)(rec[18] == save_sum(rec));
}

/* 1 when copy a is newer than copy b (wrapping sequence). */
static uint8_t save_newer(uint16_t a, uint16_t b) {
    return (uint8_t)((int8_t)(SAVE_RAM[a + 5u] - SAVE_RAM[b + 5u]) > 0);
}

void save_load(uint8_t *dst) BANKED {
    uint16_t best;
    uint8_t va, vb;

    ENABLE_RAM;
    SWITCH_RAM(0);
    va = save_valid(SAVE_SLOT_A);
    vb = save_valid(SAVE_SLOT_B);
    if (va && vb) best = save_newer(SAVE_SLOT_A, SAVE_SLOT_B) ? SAVE_SLOT_A : SAVE_SLOT_B;
    else if (va) best = SAVE_SLOT_A;
    else if (vb) best = SAVE_SLOT_B;
    else best = 0xFFFFu;

    if (best != 0xFFFFu) {
        for (sv_i = 0; sv_i < SAVE_DATA_LEN; sv_i++) {
            save_last[sv_i] = SAVE_RAM[best + 6u + sv_i];
            dst[sv_i] = save_last[sv_i];
        }
        save_seq = SAVE_RAM[best + 5u];
    } else {
        for (sv_i = 0; sv_i < SAVE_DATA_LEN; sv_i++) {
            save_last[sv_i] = 0;
            dst[sv_i] = 0;
        }
        save_seq = 0;
    }
    DISABLE_RAM;
}

/* Write one copy and verify it (RAM enabled).  1 on success. */
static uint8_t save_write(uint16_t ofs, const uint8_t *src) {
    volatile uint8_t *rec = SAVE_RAM + ofs;
    uint8_t seq = (uint8_t)(save_seq + 1u);

    for (sv_tries = 0; sv_tries < SAVE_TRIES; sv_tries++) {
        rec[0] = 0;                         /* invalidate, payload, then publish the magic */
        rec[1] = save_magic[1];
        rec[2] = save_magic[2];
        rec[3] = save_magic[3];
        rec[4] = SAVE_VERSION;
        rec[5] = seq;
        for (sv_i = 0; sv_i < SAVE_DATA_LEN; sv_i++) rec[6u + sv_i] = src[sv_i];
        rec[18] = save_sum(rec);
        rec[19] = SAVE_MARKER;
        rec[0] = save_magic[0];
        if (save_valid(ofs) && rec[5] == seq) {
            save_seq = seq;
            return 1;
        }
    }
    rec[0] = 0;
    return 0;
}

void save_store(const uint8_t *src) BANKED {
    uint8_t same = 1;
    uint16_t target;

    for (sv_i = 0; sv_i < SAVE_DATA_LEN; sv_i++) {
        if (src[sv_i] != save_last[sv_i]) same = 0;
    }
    if (same) return;

    ENABLE_RAM;
    SWITCH_RAM(0);
    /* Overwrite the stale copy (invalid, else the older one). */
    if (!save_valid(SAVE_SLOT_A)) target = SAVE_SLOT_A;
    else if (!save_valid(SAVE_SLOT_B)) target = SAVE_SLOT_B;
    else target = save_newer(SAVE_SLOT_A, SAVE_SLOT_B) ? SAVE_SLOT_B : SAVE_SLOT_A;

    if (!save_write(target, src)) {
        target = (target == SAVE_SLOT_A) ? SAVE_SLOT_B : SAVE_SLOT_A;
        (void)save_write(target, src);
    }
    for (sv_i = 0; sv_i < SAVE_DATA_LEN; sv_i++) save_last[sv_i] = src[sv_i];
    DISABLE_RAM;
}
