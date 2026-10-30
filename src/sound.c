/*
 * sound.c - 2A03 APU effects and engine hum.
 *
 * The Atari's POKEY has four channels (docs/ATARI_RE_SPEC.md section 9); they
 * map onto the four APU channels by role rather than by number:
 *
 *   pulse 1   shot: a fast downward chirp ($C6 / $7D on the Atari)
 *   pulse 2   refuelling tone ($7F) and the low-fuel warning beep ($7E)
 *   triangle  engine hum, pitched by the throttle ($B4AC table)
 *   noise     explosions ($7C)
 *
 * Everything is a countdown in frames.  sound_update() runs once per player
 * pass and advances the countdowns by game_ticks (1 to 5 frames), so the
 * length of an effect does not depend on how late the pass was.  Registers are
 * written only when the sound changes: a pulse/noise high-period write
 * restarts the channel, so the chirps write it once at their start.
 *
 * All lengths use the halt flag (constant volume, length counter held), so the
 * hardware length counters never cut a sound short; the code silences a
 * channel by writing volume 0.
 */
#include <stdint.h>

#include "sound.h"
#include "hud.h"
#include "river.h"

#define APU(a)          (*(volatile uint8_t *)(a))
#define APU_P1_CTRL     APU(0x4000)
#define APU_P1_SWEEP    APU(0x4001)
#define APU_P1_LO       APU(0x4002)
#define APU_P1_HI       APU(0x4003)
#define APU_P2_CTRL     APU(0x4004)
#define APU_P2_SWEEP    APU(0x4005)
#define APU_P2_LO       APU(0x4006)
#define APU_P2_HI       APU(0x4007)
#define APU_TRI_CTRL    APU(0x4008)
#define APU_TRI_LO      APU(0x400A)
#define APU_TRI_HI      APU(0x400B)
#define APU_NOISE_CTRL  APU(0x400C)
#define APU_NOISE_LO    APU(0x400E)
#define APU_NOISE_HI    APU(0x400F)
#define APU_STATUS      APU(0x4015)
#define APU_FRAME_CTR   APU(0x4017)

#define LEN_LOAD        0x08u   /* any non-zero length index; the halt flag holds it */
#define CONST_VOL       0x30u   /* halt + constant volume */

#define SHOT_FRAMES     12u
#define BOOM_FRAMES     36u
#define REFUEL_HOLD     4u      /* the refuel tone stays on this long after the last call */
#define LOW_FUEL        64u     /* hud_fuel below this: warning beep */

#define P2_OFF          0u
#define P2_REFUEL       1u
#define P2_WARN         2u

/* Triangle period (11 bits) per throttle step: slow, cruise, fast.  The hum
 * is about 73, 97 and 125 Hz. */
static const uint8_t engine_hi[3] = { 0x03u, 0x02u, 0x01u };
static const uint8_t engine_lo[3] = { 0x00u, 0x40u, 0xC0u };

static uint8_t snd_engine;      /* requested: 0..2 or SOUND_ENGINE_OFF */
static uint8_t snd_engine_now;  /* what the triangle is playing */
static uint8_t snd_shot;        /* frames left of the shot chirp */
static uint8_t snd_boom;        /* frames left of the explosion */
static uint8_t snd_refuel;      /* frames left of the refuel tone */
static uint8_t snd_p2;          /* P2_*: what pulse 2 is playing */
static uint8_t snd_clock;       /* free-running frame count (warning beep rhythm) */
static uint8_t snd_t;           /* scratch: this pass's frame count */
static uint8_t snd_want;        /* scratch: wanted pulse 2 sound */

void sound_init(void) {
    APU_FRAME_CTR = 0x40u;      /* 4-step sequence, frame IRQ off */
    APU_STATUS = 0x0Fu;         /* pulse 1/2, triangle, noise */
    APU_P1_SWEEP = 0u;
    APU_P2_SWEEP = 0u;
    APU_P1_CTRL = CONST_VOL;
    APU_P2_CTRL = CONST_VOL;
    APU_NOISE_CTRL = CONST_VOL;
    APU_TRI_CTRL = 0u;
    snd_engine = SOUND_ENGINE_OFF;
    snd_engine_now = SOUND_ENGINE_OFF;
    snd_shot = 0;
    snd_boom = 0;
    snd_refuel = 0;
    snd_p2 = P2_OFF;
    snd_clock = 0;
}

/* Count a frame timer down by snd_t; returns the new value. */
static uint8_t snd_tick(uint8_t v) {
    return (v > snd_t) ? (uint8_t)(v - snd_t) : 0u;
}

void sound_update(void) {
    snd_t = game_ticks;
    snd_clock = (uint8_t)(snd_clock + snd_t);

    /* Shot: square chirp falling in pitch, fading out. */
    if (snd_shot != 0u) {
        snd_shot = snd_tick(snd_shot);
        if (snd_shot != 0u) {
            APU_P1_CTRL = (uint8_t)(0x40u | CONST_VOL | snd_shot);
            APU_P1_LO = (uint8_t)(0x28u + (SHOT_FRAMES - snd_shot) * 10u);
        } else {
            APU_P1_CTRL = CONST_VOL;
        }
    }

    /* Explosion: noise that darkens (higher period index) as it fades. */
    if (snd_boom != 0u) {
        snd_boom = snd_tick(snd_boom);
        if (snd_boom != 0u) {
            APU_NOISE_CTRL = (uint8_t)(CONST_VOL | ((snd_boom + 2u) / 3u));
            APU_NOISE_LO = (uint8_t)(5u + (BOOM_FRAMES - snd_boom) / 6u);
        } else {
            APU_NOISE_CTRL = CONST_VOL;
        }
    }

    /* Pulse 2: refuelling wins over the low-fuel warning. */
    snd_refuel = snd_tick(snd_refuel);
    if (snd_refuel != 0u) {
        snd_want = P2_REFUEL;
    } else if (snd_engine != SOUND_ENGINE_OFF && hud_fuel < LOW_FUEL &&
               !(snd_clock & 8u)) {
        snd_want = P2_WARN;
    } else {
        snd_want = P2_OFF;
    }
    if (snd_want != snd_p2) {
        snd_p2 = snd_want;
        if (snd_want == P2_REFUEL) {
            APU_P2_CTRL = 0x76u;            /* duty 25%, volume 6 */
            APU_P2_LO = 0x80u;
            APU_P2_HI = LEN_LOAD;
        } else if (snd_want == P2_WARN) {
            APU_P2_CTRL = 0xB8u;            /* duty 50%, volume 8 */
            APU_P2_LO = 0x50u;
            APU_P2_HI = LEN_LOAD;
        } else {
            APU_P2_CTRL = CONST_VOL;
        }
    }

    /* Engine hum on the triangle. */
    if (snd_engine != snd_engine_now) {
        snd_engine_now = snd_engine;
        if (snd_engine == SOUND_ENGINE_OFF) {
            APU_TRI_CTRL = 0u;
        } else {
            APU_TRI_CTRL = 0x81u;           /* linear counter held at 1 */
            APU_TRI_LO = engine_lo[snd_engine];
            APU_TRI_HI = (uint8_t)(LEN_LOAD | engine_hi[snd_engine]);
        }
    }
}

void sound_set_engine(uint8_t speed) {
    snd_engine = (speed > 2u) ? SOUND_ENGINE_OFF : speed;
}

void sound_play_shot(void) {
    snd_shot = SHOT_FRAMES;
    APU_P1_CTRL = (uint8_t)(0x40u | CONST_VOL | SHOT_FRAMES);
    APU_P1_LO = 0x28u;
    APU_P1_HI = LEN_LOAD;
}

void sound_play_explosion(void) {
    snd_boom = BOOM_FRAMES;
    APU_NOISE_CTRL = (uint8_t)(CONST_VOL | 12u);
    APU_NOISE_LO = 5u;
    APU_NOISE_HI = LEN_LOAD;
}

void sound_play_refuel(void) {
    snd_refuel = REFUEL_HOLD;
}
