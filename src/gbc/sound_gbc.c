/*
 * sound_gbc.c - Game Boy APU port of the sound engine (bank 3).
 *
 * Same behaviour and timing as src/sound.c, which documents the POKEY register
 * values captured from a live Atari run.  The 2A03 and the Game Boy APU have the
 * same channel layout, so the mapping is one to one:
 *
 *      2A03 pulse 1  ->  GB CH1 (NR1x, sweep off)
 *      2A03 pulse 2  ->  GB CH2 (NR2x)
 *      2A03 noise    ->  GB CH4 (NR4x, 15-bit LFSR)
 *
 * CH3 (wave) is unused, as the triangle was.  What differs from the 2A03:
 *
 *   - Pitch.  A 2A03 pulse period p gives f = 111861 / (p + 1) Hz; the GB gives
 *     f = 131072 / (2048 - x).  G(p) converts, so the NES period tables in
 *     sound.c carry over unchanged.
 *   - Volume.  The GB only latches NRx2 on a trigger (NRx4 bit 7), and a trigger
 *     restarts the duty phase.  Constant-volume notes are triggered once and then
 *     only get pitch writes.  The notes whose volume the Atari walks every frame
 *     (tank pickup 8 -> 1, extra-life 15 -> 0) use the hardware envelope with a
 *     step of 1/64 s, which is the same slope at 60 Hz.  The noise channel has no
 *     audible phase, so its volume changes just retrigger.
 *   - Silence.  NRx2 = 0 switches the channel's DAC off, which is the cleanest
 *     way to stop a note; the next note triggers again.
 *   - Noise rate.  The GB LFSR clock is 262144 / (r * 2^s); the nearest setting
 *     to each NES noise index the engine uses is in nr43_for[].
 */
#include <gb/gb.h>
#include <stdint.h>

#include "sound.h"
#include "hud.h"        /* hud_fuel: the tank note is an octave up when full */
#include "river.h"      /* game_ticks */

/* NES pulse period -> GB 11-bit period value (131072 / 111861 = 1.17175). */
#define G(p)            ((uint16_t)(2048u - (((uint32_t)((p) + 1u) * 11717u + 5000u) / 10000u)))

#define TRIGGER         0x80u
#define DUTY_50         0x80u   /* NRx1 duty 2 */
#define ENV_CONST(v)    (uint8_t)((v) << 4)
#define ENV_DECAY(v)    (uint8_t)(((v) << 4) | 0x01u)   /* -1 every 1/64 s */

#define SHOT_FRAMES     15u
#define BOOM_HIT        23u
#define BOOM_CRASH      55u
#define BONUS_FRAMES    64u
#define BLIP_FRAMES      2u

/* NES noise period indices used by the engine (11..15). */
#define NOISE_FAST      11u
#define NOISE_CRUISE    12u
#define NOISE_SLOW      13u
#define NOISE_MIN       11u

/* NES rate (Hz) -> GB NR43 (shift << 4 | divisor), width 0 = 15 bits:
 * 3523 -> 3277, 2349 -> 2341, 1762 -> 1638, 880 -> 819, 440 -> 410. */
/* Entries 5..9 are the same five rates one octave up (shift - 1): explosions
 * use idx + NOISE_UP.  Set NOISE_UP to 0 to revert. */
#define NOISE_UP        5u
static const uint8_t nr43_for[10] = {
    0x45u, 0x47u, 0x55u, 0x65u, 0x75u,
    0x35u, 0x37u, 0x45u, 0x55u, 0x65u
};

static const uint16_t shot_period[SHOT_FRAMES] = {
    G(255u), G(276u), G(297u), G(318u), G(339u), G(360u), G(381u), G(402u),
    G(423u), G(444u), G(465u), G(486u), G(507u), G(528u), G(549u)
};

static const uint16_t bonus_period[4] = { G(339u), G(283u), G(226u), G(170u) };

#define FUEL_FRAMES     8u
#define FUEL_PERIOD     G(223u)         /* 499 Hz */
#define FUEL_PERIOD_FULL G(111u)        /* 999 Hz: one octave up */
#define FUEL_HOLD       3u

#define SHELL_FRAMES    57u
#define SHELL_CHIRP     16u

static const uint16_t shell_period[SHELL_CHIRP] = {
    G(52u), G(62u), G(76u), G(90u), G(108u), G(129u), G(153u), G(185u),
    G(220u), G(262u), G(314u), G(377u), G(450u), G(542u), G(650u), G(780u)
};

#define ENGINE_STEP_FRAMES      8u
static const uint8_t engine_vol[3] = { 3u, 4u, 6u };
static const uint8_t engine_idx[3] = { NOISE_SLOW, NOISE_CRUISE, NOISE_FAST };

#define P2_OFF          0u
#define P2_BONUS        2u
#define P2_BLIP         3u

#define MISSILE_FRAMES  9u
static const uint8_t missile_noise[MISSILE_FRAMES] = {
    14u, 13u, 13u, 13u, 13u, 12u, 12u, 11u, 11u
};

#define LOW_FUEL        64u
#define WARN_MIN        28u
#define WARN_RELOAD     0x3Eu

static uint8_t snd_engine;
static uint8_t snd_shot;
static uint8_t snd_boom;
static uint8_t snd_p2;
static uint8_t snd_p2_t;
static uint8_t snd_bonus_step;  /* arpeggio step whose note has been triggered */
static uint8_t snd_fuel;
static uint8_t snd_fuel_t;
static uint8_t snd_fuel_full;
static uint8_t snd_warn;
uint8_t sound_warn_on;              /* 1 while the low-fuel note is audible (read by the HUD) */
static uint8_t snd_shell;
static uint8_t snd_msl;
static uint8_t snd_p2_on;
static uint8_t snd_eng_idx;
static uint8_t snd_eng_wait;
static uint8_t snd_noise_idx;   /* shadow: noise index on the APU */
static uint8_t snd_noise_vol;   /* shadow: noise volume on the APU */
static uint8_t snd_p1_env;      /* shadow of NR12 (0 = channel off) */
static uint8_t snd_p2_env;      /* shadow of NR22 */
static uint8_t snd_rand;
static uint8_t snd_t;

/* Pulse 1 / 2 note.  A trigger restarts the note, so it is sent only when the
 * envelope byte changes or the caller forces it; otherwise only the pitch is
 * written, which glides without a click. */
static void p1_note(uint16_t x, uint8_t env, uint8_t force) {
    NR13_REG = (uint8_t)x;
    if (force || env != snd_p1_env) {
        snd_p1_env = env;
        NR12_REG = env;
        NR14_REG = (uint8_t)(TRIGGER | (x >> 8));
    } else {
        NR14_REG = (uint8_t)(x >> 8);
    }
}

static void p1_off(void) {
    if (snd_p1_env != 0u) {
        snd_p1_env = 0u;
        NR12_REG = 0u;
    }
}

static void p2_note(uint16_t x, uint8_t env, uint8_t force) {
    NR23_REG = (uint8_t)x;
    if (force || env != snd_p2_env) {
        snd_p2_env = env;
        NR22_REG = env;
        NR24_REG = (uint8_t)(TRIGGER | (x >> 8));
    } else {
        NR24_REG = (uint8_t)(x >> 8);
    }
}

static void p2_off(void) {
    if (snd_p2_env != 0u) {
        snd_p2_env = 0u;
        NR22_REG = 0u;
    }
}

static void snd_set_noise(uint8_t idx, uint8_t vol) {
    if (idx != snd_noise_idx) {
        snd_noise_idx = idx;
        NR43_REG = nr43_for[(uint8_t)(idx - NOISE_MIN)];
    }
    if (vol != snd_noise_vol) {
        snd_noise_vol = vol;
        NR42_REG = (uint8_t)(vol << 4);
        if (vol != 0u) {
            NR44_REG = TRIGGER;
        }
    }
}

/* Start a noise burst even if the volume matches the current one. */
static void snd_noise_hit(uint8_t idx, uint8_t vol) {
    snd_noise_vol = 0u;
    snd_set_noise(idx, vol);
}

static uint8_t snd_tick(uint8_t v) {
    return (v > snd_t) ? (uint8_t)(v - snd_t) : 0u;
}

static uint8_t snd_next_rand(void) {
    uint8_t r = snd_rand;

    r ^= (uint8_t)(r << 3);
    r ^= (uint8_t)(r >> 5);
    r = (uint8_t)(r + 0x1Du);
    snd_rand = r;
    return r;
}

void sound_init(void) BANKED {
    NR52_REG = 0x80u;           /* APU on */
    NR50_REG = 0x77u;           /* both terminals at full master volume */
    NR51_REG = 0xFFu;           /* every channel to both terminals */
    NR10_REG = 0u;              /* sweep off */
    NR11_REG = DUTY_50;
    NR21_REG = DUTY_50;
    NR12_REG = 0u;
    NR22_REG = 0u;
    NR42_REG = 0u;
    NR30_REG = 0u;              /* wave DAC off */
    NR43_REG = nr43_for[NOISE_CRUISE - NOISE_MIN];
    snd_engine = SOUND_ENGINE_OFF;
    snd_shot = 0;
    snd_boom = 0;
    snd_p2 = P2_OFF;
    snd_p2_t = 0;
    snd_bonus_step = 0;
    snd_fuel = 0;
    snd_fuel_t = 0;
    snd_fuel_full = 0;
    snd_warn = 0;
    sound_warn_on = 0;
    snd_shell = 0;
    snd_msl = 0;
    snd_p2_on = 0;
    snd_eng_idx = NOISE_CRUISE;
    snd_eng_wait = 0;
    snd_noise_idx = NOISE_CRUISE;
    snd_noise_vol = 0;
    snd_p1_env = 0;
    snd_p2_env = 0;
    snd_rand = 0x5Au;
}

void sound_update(void) BANKED {
    uint16_t p;
    uint8_t step;
    uint8_t vol;
    uint8_t idx;

    snd_t = game_ticks;

    /* Low-fuel warning: owns pulse 1 while the fuel stays below $40 (see
     * sound.c).  AUDF1 = 2*w gives f = 31960 / (2w + 1); the GB value
     * x = 2048 - 131072 / f is linear in w: about 2044 - 8.2 w. */
    if (snd_engine != SOUND_ENGINE_OFF && hud_fuel < LOW_FUEL) {
        snd_warn = (snd_warn > snd_t) ? (uint8_t)(snd_warn - snd_t) : WARN_RELOAD;
        if (snd_warn >= WARN_MIN) {
            p = (uint16_t)(2044u - ((uint16_t)snd_warn << 3)
                           - (uint8_t)(((uint16_t)snd_warn * 51u) >> 8));
            p1_note(p, ENV_CONST(15u), 0u);
            sound_warn_on = 1u;
        } else if (sound_warn_on != 0u) {
            sound_warn_on = 0u;
            p1_off();
        }
    } else if (sound_warn_on != 0u) {
        sound_warn_on = 0u;
        p1_off();
    }

    /* Shot: pulse 1, 405 -> 196 Hz in 15 steps, volume 8. */
    if (snd_shot != 0u) {
        snd_shot = snd_tick(snd_shot);
        if (sound_warn_on == 0u) {
            if (snd_shot != 0u) {
                p1_note(shot_period[(uint8_t)(SHOT_FRAMES - snd_shot)], ENV_CONST(8u), 0u);
            } else {
                p1_off();
            }
        }
    }

    if (snd_shell != 0u) {
        snd_shell = (uint8_t)(snd_shell + snd_t);
        if (snd_shell >= SHELL_FRAMES) {
            snd_shell = 0u;
        }
    }

    /* Pulse 2: tank note, shell chirp, extra-life arpeggio, blip. */
    if (snd_fuel != 0u) {
        snd_fuel = snd_tick(snd_fuel);
    }
    if (snd_fuel_t == 0u && snd_fuel != 0u) {
        /* Re-arm: one triggered note whose envelope walks 8 -> 1 on its own. */
        snd_fuel_t = FUEL_FRAMES;
        snd_p2 = P2_OFF;
        p2_note((snd_fuel_full != 0u) ? FUEL_PERIOD_FULL : FUEL_PERIOD, ENV_DECAY(8u), 1u);
    }
    if (snd_fuel_t != 0u) {
        snd_fuel_t = snd_tick(snd_fuel_t);
        snd_p2_on = 1u;
    } else if (snd_shell != 0u && snd_shell <= SHELL_CHIRP) {
        p2_note(shell_period[(uint8_t)(snd_shell - 1u)], ENV_CONST(12u), 0u);
        snd_p2 = P2_OFF;
        snd_p2_on = 1u;
    } else if (snd_p2 != P2_OFF) {
        snd_p2_t = snd_tick(snd_p2_t);
        if (snd_p2_t == 0u) {
            snd_p2 = P2_OFF;
            snd_p2_on = 0u;
            p2_off();
        } else if (snd_p2 == P2_BONUS) {
            step = (uint8_t)((uint8_t)(BONUS_FRAMES - snd_p2_t) >> 4);
            p2_note(bonus_period[step], ENV_DECAY(15u), (uint8_t)(step != snd_bonus_step));
            snd_bonus_step = step;
            snd_p2_on = 1u;
        } else {
            snd_p2_on = 1u;         /* P2_BLIP: triggered at its start */
        }
    } else if (snd_p2_on != 0u) {
        snd_p2_on = 0u;
        p2_off();
    }

    /* Noise: explosion, then the helicopter zap, then the shell's fireball, then
     * the engine hiss. */
    idx = snd_noise_idx;
    vol = 0u;
    if (snd_boom != 0u) {
        snd_boom = snd_tick(snd_boom);
        if (snd_boom != 0u) {
            vol = (uint8_t)(((snd_boom >> 1) + 4u) & 0x0Fu);
            idx = (uint8_t)(12u + NOISE_UP + (snd_next_rand() & 3u));
        }
    }
    if (snd_msl != 0u) {
        snd_msl = snd_tick(snd_msl);
        if (snd_msl != 0u) {
            vol = (uint8_t)(snd_msl + 6u);
            idx = missile_noise[(uint8_t)(snd_msl - 1u)];
        }
    }
    if (vol == 0u && snd_shell > SHELL_CHIRP) {
        vol = 12u;
        idx = (uint8_t)(12u + (snd_next_rand() & 3u));
    }
    if (vol == 0u && snd_engine != SOUND_ENGINE_OFF && sound_warn_on == 0u) {
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

void sound_set_engine(uint8_t speed) BANKED {
    snd_engine = (speed > 2u) ? SOUND_ENGINE_OFF : speed;
}

void sound_play_shot(void) BANKED {
    snd_shot = SHOT_FRAMES;
    if (sound_warn_on == 0u) {
        p1_note(shot_period[0], ENV_CONST(8u), 1u);
    }
}

void sound_play_pickup(void) BANKED {
    snd_fuel = FUEL_HOLD;
    snd_fuel_full = (hud_fuel >= 255u) ? 1u : 0u;
}

void sound_play_explosion(void) BANKED {
    snd_boom = BOOM_HIT;
    snd_noise_hit(13u + NOISE_UP, 15u);
}

void sound_play_crash(void) BANKED {
    snd_boom = BOOM_CRASH;
    snd_noise_hit(13u + NOISE_UP, 15u);
}

void sound_play_shell(void) BANKED {
    snd_shell = 1u;
    snd_p2_on = 1u;
    snd_p2 = P2_OFF;
    p2_note(shell_period[0], ENV_CONST(12u), 1u);
}

void sound_play_missile(void) BANKED {
    snd_msl = MISSILE_FRAMES;
    snd_noise_hit(missile_noise[MISSILE_FRAMES - 1u], 15u);
}

void sound_play_bonus(void) BANKED {
    snd_p2 = P2_BONUS;
    snd_p2_on = 1u;
    snd_p2_t = BONUS_FRAMES;
    snd_bonus_step = 0u;
    p2_note(bonus_period[0], ENV_DECAY(15u), 1u);
}

void sound_play_blip(void) BANKED {
    snd_p2 = P2_BLIP;
    snd_p2_on = 1u;
    snd_p2_t = BLIP_FRAMES;
    p2_note(G(226u), ENV_CONST(15u), 1u);       /* 492 Hz */
}
