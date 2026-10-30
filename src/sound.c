/*
 * sound.c - 2A03 APU effects and engine noise.
 *
 * The Atari runs four POKEY channels from the deferred VBI at $B2A5.  With
 * AUDCTL = 0 (the cart never writes it) all four use the 64 kHz clock, so a
 * channel's tone is
 *
 *      f = 63920 / (2 * (AUDF + 1))
 *
 * and AUDC carries the volume in the low nibble and the noise/distortion
 * selector in bits 7-5 ($00/$20/$40/$60 = polynomials, $80 = 17-bit poly,
 * $A0/$C0/$E0 = pure tone).  The matching 2A03 pulse period is
 *
 *      p = 111861 / f - 1
 *
 * and the noise channel's LFSR rate is 1789773 / noise_period[p], with
 * noise_period = 4 8 16 32 64 96 128 160 202 254 380 508 762 1016 ...
 *
 * Everything below was read off a live Altirra run frame by frame
 * (tools/atari_sound_trace.py, tools/atari_sound_events.py); the values are
 * the register writes the original actually makes, not estimates:
 *
 *   ch1  engine   17-bit poly noise, AUDC1 = $B4AC[joystick & 3]:
 *                 $86 (vol 6) accelerating, $84 (vol 4) level, $83 (vol 3)
 *                 slowing, $00 silent.  AUDF1 = $1F - (jet Y >> 4) gives an
 *                 LFSR rate of ~2.3-2.7 kHz.
 *   ch1  blip     $C6 = 2 (start/option): AUDF1 = $40 (492 Hz), AUDC1 = $AF
 *                 (pure tone, vol 15), two frames.
 *   ch2  explosion $7C = $17 (object destroyed) or $37 (jet destroyed):
 *                 AUDC2 = $80 | (((t>>1)+4) & $0F) - poly noise with a
 *                 sawtooth volume - and AUDF2 = RANDOM | $18, so the pitch
 *                 is random every frame.  23 or 55 frames.
 *   ch1  pickup   while the jet is over a fuel tank ($A40A sets $7A = 4,
 *                 $7B = 8): AUDF1 = $3F (a constant 499 Hz) and
 *                 AUDC1 = $7B + $A0 = $A8..$A1, a pure tone whose volume walks
 *                 8 down to 1 over 8 frames.  The VBI clears $7A when the sweep
 *                 ends, so the next frame over the tank re-arms it: one note
 *                 repeated back-to-back for as long as the jet stays on it.
 *   ch1  warning  $5B < $40 (fuel below a quarter): the engine gives up the
 *                 channel and a pure tone at volume 15 plays with
 *                 AUDF1 = 2 * $7B, where $7B counts $3E down to $00 and the
 *                 tone only sounds while $7B >= $1C: 35 frames rising from
 *                 251 Hz to 561 Hz, then 28 frames of engine, repeating.
 *   ch3  shot     $7D = $0F: pure tone, AUDC3 = $A8 (vol 8) and
 *                 AUDF3 = 6*(27-$7D), i.e. 405 Hz falling to 196 Hz in 15
 *                 one-frame steps.  This is the fire button.
 *   ch3  bonus    $7E = $40 (score crosses 10,000): pure tone in four rising
 *                 steps, AUDF3 = $60/$50/$40/$30 (329/394/492/652 Hz), 16
 *                 frames each, with AUDC3 = $A0 + (t & $0F), a 15..0 volume
 *                 ramp inside every step.  64 frames.
 *
 * Not a sound source: $7F = 9 ($A5E0) is the shot passing a helicopter
 * (object types $0A/$0B), not the fuel tank.
 *
 * The 2A03 has one noise channel where POKEY has three, so the explosion takes
 * it over and the engine hiss resumes when it finishes.  The triangle is unused
 * (the original has no comparable voice).
 *
 * Everything is a countdown in 60 Hz frames.  sound_update() runs every frame
 * and advances the countdowns by game_ticks (1, more after an overrun), so an
 * effect's length does not depend on how late the pass was.  Registers are
 * written only when the sound changes: a pulse/noise high-period write
 * restarts the channel, so the chirps write it once at their start.
 *
 * All lengths use the halt flag (constant volume, length counter held), so the
 * hardware length counters never cut a sound short; the code silences a
 * channel by writing volume 0.
 */
#include <stdint.h>

#include "sound.h"
#include "hud.h"        /* hud_fuel: the tank note is an octave up when full */
#include "river.h"      /* game_ticks */

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
#define APU_NOISE_CTRL  APU(0x400C)
#define APU_NOISE_LO    APU(0x400E)
#define APU_NOISE_HI    APU(0x400F)
#define APU_STATUS      APU(0x4015)
#define APU_FRAME_CTR   APU(0x4017)

#define LEN_LOAD        0x08u   /* any non-zero length index; the halt flag holds it */
#define CONST_VOL       0x30u   /* halt + constant volume */
#define DUTY_12         0x00u   /* 2A03 duty 0 = 12.5% */
#define DUTY_25         0x40u   /* 2A03 duty 1 = 25% */
#define DUTY_50         0x80u   /* 2A03 duty 2 = 50%: POKEY's "no poly" is a square */
#define PULSE(duty, v)  (uint8_t)((duty) | CONST_VOL | (v))

#define SHOT_FRAMES     15u
#define BOOM_HIT        23u
#define BOOM_CRASH      55u
#define BONUS_FRAMES    64u
#define BLIP_FRAMES      2u

/* Noise period indices (see the table in the header comment). */
#define NOISE_FAST      11u     /* 3523 Hz */
#define NOISE_CRUISE    12u     /* 2349 Hz */
#define NOISE_SLOW      13u     /* 1762 Hz */

/* Shot sweep, 405 Hz down to 196 Hz: p = 255 + 21*step (pulse period). */
static const uint16_t shot_period[SHOT_FRAMES] = {
    255u, 276u, 297u, 318u, 339u, 360u, 381u, 402u,
    423u, 444u, 465u, 486u, 507u, 528u, 549u
};

/* Extra-life arpeggio: 329.5, 394.6, 491.7, 652.2 Hz. */
static const uint16_t bonus_period[4] = { 339u, 283u, 226u, 170u };

/* Fuel tank ($A40A sets $7A = 4, $7B = 8 while the jet is over it; the VBI then
 * plays $B2D9-$B2ED): AUDF1 is $3F (499 Hz) normally, but when the fuel
 * increment carries - i.e. $5B is already $FF, gauge full - the code saturates
 * $5B and selects $7A = 3, whose path keeps AUDF1 = $1F (999 Hz): one octave
 * up.  AUDC1 = $7B + $A0 = $A8..$A1 is a pure tone whose volume walks 8 down
 * to 1 over 8 frames.  The VBI clears $7A when the sweep ends, so the next
 * frame over the tank re-arms it: one note repeated back-to-back for as long
 * as the jet stays on the tank. */
#define FUEL_FRAMES     8u
#define FUEL_PERIOD     223u    /* AUDF1 = $3F -> 499 Hz: 111861/499.4 - 1 */
#define FUEL_PERIOD_FULL 111u   /* AUDF1 = $1F -> 999 Hz: one octave up */
#define FUEL_HOLD       3u      /* frames a sound_play_pickup() call keeps the note alive */

/* Enemy shell (Atari channel 4, driven by $50 in $A609-$A674): for $50 = 1..$10
 * the VBI plays the $B4C0 pitch table with AUDC4 = $AC (pure tone, volume 12),
 * a four-octave dive from 2131 Hz to 143 Hz; for $50 = $11..$38 it plays
 * AUDF4 = RANDOM|$20 with AUDC4 = $8C (17-bit poly, volume 12) while the shell
 * swells into a fireball.  $50 = 0/$FF is silent. */
#define SHELL_FRAMES    57u     /* $50 counts 0..$38 */
#define SHELL_CHIRP     16u     /* $50 = $01..$10 */

/* $B4C0 -> 2A03 pulse periods (p = 111861/f - 1 for AUDF 0E,11,15,19,1E,24,2B,
 * 34,3E,4A,59,6B,80,9A,B9,DE). */
static const uint16_t shell_period[SHELL_CHIRP] = {
    52u, 62u, 76u, 90u, 108u, 129u, 153u, 185u,
    220u, 262u, 314u, 377u, 450u, 542u, 650u, 780u
};

/* AUDC1 = $B4AC[joystick & 3] is volume 6 accelerating, 4 level, 3 slowing.
 * The pitch follows the jet's altitude (AUDF1 = $1F - ($59 >> 4)), which only
 * moves a pixel at a time, so the throttle is heard as a slide and not as a
 * jump: on the 2A03 the three noise indices are walked one at a time. */
#define ENGINE_STEP_FRAMES      8u      /* frames per noise-index step */
static const uint8_t engine_vol[3] = { 3u, 4u, 6u };    /* slow, cruise, fast */
static const uint8_t engine_idx[3] = { NOISE_SLOW, NOISE_CRUISE, NOISE_FAST };

/* What pulse 2 is playing. */
#define P2_OFF          0u
#define P2_BONUS        2u
#define P2_BLIP         3u

static uint8_t snd_engine;      /* requested: 0..2 or SOUND_ENGINE_OFF */
static uint8_t snd_shot;        /* frames left of the shot sweep */
static uint8_t snd_boom;        /* frames left of the explosion */
static uint8_t snd_p2;          /* P2_*: pulse 2 job */
static uint8_t snd_p2_t;        /* frames left of that job */
static uint8_t snd_fuel;        /* frames a fuel-tank contact keeps the note armed */
static uint8_t snd_fuel_t;      /* 8..1 within the current repeated note */
/* Low-fuel warning ($B2FD-$B32A): below $40 of fuel the Atari takes channel 1
 * from the engine, plays a volume-15 pure tone whose AUDF1 is 2*$7B, and counts
 * $7B from $3E down to $00; the tone sounds only while $7B >= $1C, so 35 frames
 * of rising tone (251 -> 561 Hz) then 28 frames of engine, repeating. */
#define LOW_FUEL        64u     /* $40: the $5B < $40 test */
#define WARN_MIN        28u     /* $1C: the tone stops below this */
#define WARN_RELOAD     0x3Eu   /* $7B reloads to $3E ($3F is skipped by dey) */

static uint8_t snd_fuel_full;   /* 1 = gauge full: the note is an octave up */
static uint8_t snd_warn;        /* the Atari's $7B: $3E .. $00 */
static uint8_t snd_warn_on;     /* 1 while the warning tone owns pulse 1 */
static uint8_t snd_shell;       /* the Atari's $50: 0 idle, 1..56 = playing */
static uint8_t snd_p2_on;       /* 1 while pulse 2 still holds a tone */
static uint8_t snd_eng_idx;     /* the glide's current noise index */
static uint8_t snd_eng_wait;    /* frames spent on the current glide step */
static uint8_t snd_noise_idx;   /* shadow: noise period index on the APU */
static uint8_t snd_noise_vol;   /* shadow: noise volume on the APU */
static uint8_t snd_rand;        /* explosion pitch noise */
static uint8_t snd_t;           /* scratch: frames this pass */

void sound_init(void) {
    APU_FRAME_CTR = 0x40u;      /* 4-step sequence, frame IRQ off */
    APU_STATUS = 0x0Fu;         /* pulse 1/2, triangle, noise */
    APU_P1_SWEEP = 0u;
    APU_P2_SWEEP = 0u;
    APU_P1_CTRL = CONST_VOL;
    APU_P2_CTRL = CONST_VOL;
    APU_NOISE_CTRL = CONST_VOL;
    APU_TRI_CTRL = 0u;
    APU_NOISE_LO = NOISE_CRUISE;
    /* The length counter silences a channel while it is zero, even with the
     * halt flag set: load it once so the noise channel (the engine hiss) can
     * speak before the first effect.  $4003/$4007 reload theirs on every
     * pulse write. */
    APU_NOISE_HI = LEN_LOAD;
    snd_engine = SOUND_ENGINE_OFF;
    snd_shot = 0;
    snd_boom = 0;
    snd_p2 = P2_OFF;
    snd_p2_t = 0;
    snd_fuel = 0;
    snd_fuel_t = 0;
    snd_fuel_full = 0;
    snd_warn = 0;
    snd_warn_on = 0;
    snd_shell = 0;
    snd_p2_on = 0;
    snd_eng_idx = NOISE_CRUISE;
    snd_eng_wait = 0;
    snd_noise_idx = NOISE_CRUISE;
    snd_noise_vol = 0;
    snd_rand = 0x5Au;
}

/* Count a frame timer down by snd_t; returns the new value. */
static uint8_t snd_tick(uint8_t v) {
    return (v > snd_t) ? (uint8_t)(v - snd_t) : 0u;
}

/* 8-bit xorshift: the explosion picks a new POKEY AUDF2 (RANDOM | $18) every
 * frame, which on the noise channel is a new period index. */
static uint8_t snd_next_rand(void) {
    uint8_t r = snd_rand;

    r ^= (uint8_t)(r << 3);
    r ^= (uint8_t)(r >> 5);
    r = (uint8_t)(r + 0x1Du);
    snd_rand = r;
    return r;
}

static void snd_set_noise(uint8_t idx, uint8_t vol) {
    if (idx != snd_noise_idx) {
        snd_noise_idx = idx;
        APU_NOISE_LO = idx;
    }
    if (vol != snd_noise_vol) {
        snd_noise_vol = vol;
        APU_NOISE_CTRL = (uint8_t)(CONST_VOL | vol);
    }
}

void sound_update(void) {
    uint16_t p;
    uint8_t step;
    uint8_t vol;
    uint8_t idx;

    snd_t = game_ticks;

    /* Low-fuel warning ($B2FD-$B32A) has priority over every other sound: it
     * owns pulse 1 for as long as the fuel stays below $40, and nothing else
     * may write that channel while the tone is sounding.  Below $40 the Atari
     * takes channel 1 from the engine, plays a volume-15 pure tone whose AUDF1
     * is 2*$7B, and counts $7B from $3E down to $00; the tone sounds only while
     * $7B >= $1C, so 35 frames of rising tone (251 -> 561 Hz) then 28 frames
     * where the engine returns. */
    if (snd_engine != SOUND_ENGINE_OFF && hud_fuel < LOW_FUEL) {
        snd_warn = (snd_warn > snd_t) ? (uint8_t)(snd_warn - snd_t) : WARN_RELOAD;
        if (snd_warn >= WARN_MIN) {
            p = (uint16_t)((uint16_t)snd_warn * 7u + 3u);
            APU_P1_LO = (uint8_t)p;
            APU_P1_HI = (uint8_t)(LEN_LOAD | (p >> 8));
            APU_P1_CTRL = PULSE(DUTY_50, 15u);
            snd_warn_on = 1u;
        } else if (snd_warn_on != 0u) {
            snd_warn_on = 0u;
            APU_P1_CTRL = CONST_VOL;
        }
    } else if (snd_warn_on != 0u) {
        snd_warn_on = 0u;
        APU_P1_CTRL = CONST_VOL;
    }

    /* Shot: pulse 1, 405 Hz falling to 196 Hz, 15 steps, volume 8.  It only
     * touches the channel when the warning is not using it; the countdown still
     * runs so a suppressed shot does not linger. */
    if (snd_shot != 0u) {
        snd_shot = snd_tick(snd_shot);
        if (snd_warn_on == 0u) {
            if (snd_shot != 0u) {
                p = shot_period[(uint8_t)(SHOT_FRAMES - snd_shot)];
                APU_P1_LO = (uint8_t)p;
                APU_P1_HI = (uint8_t)(LEN_LOAD | (p >> 8));
            } else {
                APU_P1_CTRL = CONST_VOL;
            }
        }
    }

    /* Enemy shell state ($50): advance and wrap at the end of the cycle. */
    if (snd_shell != 0u) {
        snd_shell = (uint8_t)(snd_shell + snd_t);
        if (snd_shell >= SHELL_FRAMES) {
            snd_shell = 0u;
        }
    }

    /* Pulse 2: the fuel-tank note (highest priority), then the shell chirp,
     * then the extra-life arpeggio, then the blip.  snd_p2_on tracks whether
     * the last write was a tone, so the channel is always silenced when the
     * last job ends - the tank note's hold flag outlives its final volume-1
     * frame, which used to leave it hanging at volume 1. */
    if (snd_fuel != 0u) {
        snd_fuel = snd_tick(snd_fuel);
    }
    if (snd_fuel_t == 0u && snd_fuel != 0u) {
        /* Re-arm the note: 499 Hz, or 999 Hz while the gauge is full. */
        snd_fuel_t = FUEL_FRAMES;
        snd_p2 = P2_OFF;
        p = (snd_fuel_full != 0u) ? FUEL_PERIOD_FULL : FUEL_PERIOD;
        APU_P2_LO = (uint8_t)p;
        APU_P2_HI = (uint8_t)(LEN_LOAD | (p >> 8));
    }
    if (snd_fuel_t != 0u) {
        APU_P2_CTRL = PULSE(DUTY_50, snd_fuel_t);
        snd_fuel_t = snd_tick(snd_fuel_t);
        snd_p2_on = 1u;
    } else if (snd_shell != 0u && snd_shell <= SHELL_CHIRP) {
        p = shell_period[(uint8_t)(snd_shell - 1u)];
        APU_P2_LO = (uint8_t)p;
        APU_P2_HI = (uint8_t)(LEN_LOAD | (p >> 8));
        APU_P2_CTRL = PULSE(DUTY_50, 12u);
        snd_p2 = P2_OFF;
        snd_p2_on = 1u;
    } else if (snd_p2 != P2_OFF) {
        snd_p2_t = snd_tick(snd_p2_t);
        if (snd_p2_t == 0u) {
            snd_p2 = P2_OFF;
            snd_p2_on = 0u;
            APU_P2_CTRL = CONST_VOL;
        } else if (snd_p2 == P2_BONUS) {
            step = (uint8_t)(BONUS_FRAMES - snd_p2_t);
            p = bonus_period[step >> 4];
            APU_P2_LO = (uint8_t)p;
            APU_P2_HI = (uint8_t)(LEN_LOAD | (p >> 8));
            APU_P2_CTRL = PULSE(DUTY_50, (uint8_t)(15u - (step & 0x0Fu)));
            snd_p2_on = 1u;
        } else {
            snd_p2_on = 1u;         /* P2_BLIP: written at its trigger */
        }
    } else if (snd_p2_on != 0u) {
        snd_p2_on = 0u;
        APU_P2_CTRL = CONST_VOL;
    }

    /* Noise: the explosion wins, then the shell's swelling fireball, then the
     * engine hiss.  POKEY plays the warning on the engine's own channel, so the
     * engine is silent while it sounds and returns in the 28-frame gap. */
    idx = snd_noise_idx;
    vol = 0u;
    if (snd_boom != 0u) {
        snd_boom = snd_tick(snd_boom);
        if (snd_boom != 0u) {
            vol = (uint8_t)(((snd_boom >> 1) + 4u) & 0x0Fu);
            idx = (uint8_t)(12u + (snd_next_rand() & 3u));
        }
    }
    if (vol == 0u && snd_shell > SHELL_CHIRP) {
        /* $50 >= $11: random-pitch 17-bit poly while the fireball swells. */
        vol = 12u;
        idx = (uint8_t)(12u + (snd_next_rand() & 3u));
    }
    if (vol == 0u && snd_engine != SOUND_ENGINE_OFF && snd_warn_on == 0u) {
        /* Glide the hiss: one noise index every ENGINE_STEP_FRAMES frames. */
        if (snd_eng_idx != engine_idx[snd_engine]) {
            snd_eng_wait = (uint8_t)(snd_eng_wait + snd_t);
            if (snd_eng_wait >= ENGINE_STEP_FRAMES) {
                snd_eng_wait = 0u;
                snd_eng_idx = (snd_eng_idx < engine_idx[snd_engine])
                    ? (uint8_t)(snd_eng_idx + 1u) : (uint8_t)(snd_eng_idx - 1u);
            }
        } else {
            snd_eng_wait = 0u;
        }
        vol = engine_vol[snd_engine];
        idx = snd_eng_idx;
    }
    snd_set_noise(idx, vol);
}

void sound_set_engine(uint8_t speed) {
    snd_engine = (speed > 2u) ? SOUND_ENGINE_OFF : speed;
}

void sound_play_shot(void) {
    snd_shot = SHOT_FRAMES;
    if (snd_warn_on == 0u) {        /* the low-fuel tone must not be interrupted */
        APU_P1_CTRL = PULSE(DUTY_50, 8u);
        APU_P1_LO = (uint8_t)shot_period[0];
        APU_P1_HI = (uint8_t)(LEN_LOAD | (shot_period[0] >> 8));
    }
}

void sound_play_pickup(void) {
    /* Called every frame the jet is over the tank.  The increment at $A40C only
     * carries - selecting the octave-up note - when the gauge is already full
     * ($5B = $FF), which is why a full tank sounds higher. */
    snd_fuel = FUEL_HOLD;
    snd_fuel_full = (hud_fuel >= 255u) ? 1u : 0u;
}

void sound_play_explosion(void) {
    snd_boom = BOOM_HIT;
    APU_NOISE_LO = 13u;
    APU_NOISE_CTRL = (uint8_t)(CONST_VOL | 15u);
    snd_noise_idx = 13u;
    snd_noise_vol = 15u;
}

void sound_play_crash(void) {
    snd_boom = BOOM_CRASH;
    APU_NOISE_LO = 13u;
    APU_NOISE_CTRL = (uint8_t)(CONST_VOL | 15u);
    snd_noise_idx = 13u;
    snd_noise_vol = 15u;
}

void sound_play_shell(void) {
    /* Called from ent_shell_init(), i.e. every time the tank (re)fires. */
    snd_shell = 1u;
    snd_p2_on = 1u;
    APU_P2_LO = (uint8_t)shell_period[0];
    APU_P2_HI = (uint8_t)(LEN_LOAD | (shell_period[0] >> 8));
    APU_P2_CTRL = PULSE(DUTY_50, 12u);
    snd_p2 = P2_OFF;            /* the chirp outranks the arpeggio/blip */
}

void sound_play_bonus(void) {
    snd_p2 = P2_BONUS;
    snd_p2_on = 1u;
    snd_p2_t = BONUS_FRAMES;
    APU_P2_LO = (uint8_t)bonus_period[0];
    APU_P2_HI = (uint8_t)(LEN_LOAD | (bonus_period[0] >> 8));
    APU_P2_CTRL = PULSE(DUTY_50, 15u);
}

void sound_play_blip(void) {
    snd_p2 = P2_BLIP;
    snd_p2_on = 1u;
    snd_p2_t = BLIP_FRAMES;
    APU_P2_LO = 226u;           /* 492 Hz, the Atari's $C6 blip */
    APU_P2_HI = LEN_LOAD;
    APU_P2_CTRL = PULSE(DUTY_50, 15u);
}
