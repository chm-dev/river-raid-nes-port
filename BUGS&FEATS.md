# BUGS & FEATS — GBC port

Status: `[ ]` open · `[~]` fixed, waiting for the user's playtest · `[x]` confirmed in play by the user.
The NES list is in `archive/nes/BUGS&FEATS.md`.

# BUGS
Found in the milestone 1 playtest (`roms/m1_jet.gb`, 2026-10-31). All expected at this stage; not blocking.
- [x] **Player shot is invisible** (fixed in m2_hud: logic OAM had 40 slots, the shot is slot 47; now 48 packed into 40) - was: (it still destroys enemies). Likely the shot sprite's tile or palette; sprites are not yet set up for GBC. See `player.c` shot slot 47 and `entities_shot_tile()`.
- [x] **Enemy sprites are black** (m2_hud sets RGB sprite palettes in `gbc/main.c`; no generator change yet) - was: (size and direction are right). Entity palettes still hold NES values; `tools/gen_entity_gfx.py` needs GBC palette output, then `set_sprite_palette` for them. Milestone 3 colour work, earlier if cheap.
- [ ] **Shots destroy enemies with a delay.** Seen in the m2 playtest (shot and enemy colours confirmed fine): usually feels off, sometimes fine, as if the hit logic lagged the visuals. Suspect the 30 Hz logic (player/shot/collision every 2nd frame) against the 60 Hz scroll and sprite draw; the shot moves 6 px x ticks per logic pass, so it jumps 12 px and may be tested a frame after it is drawn. Check the shot/entity order and whether ntities_hit_shot uses the same positions that are drawn before changing LOGIC_FRAMES.
- [ ] **Some side hills and trees glitch.** Most are fine. Not diagnosed; suspects: the unsplit tile job and the direct upload path writing while the display is on, or margin handling now that `RIVER_LEFT_COL` is 0. Check with `tests/compare_tile_rows.mjs` / `tests/host_river.ps1` before guessing.
- [ ] **Occasional stutter at fast speed.** Smooth otherwise and acceptable. Expected: `river_run_job` is not split across frames yet (plan: columns-per-frame limit). Measure before fixing.

# FEATURES
- [ ] **Move the jet lower on screen.** The 128 px view anchored at logic scanline 112 leaves the jet too far from the bottom and the playfield too short ahead of it. Tune `GBC_VIEW_TOP_SCANLINE` / the jet's Y (`config_gbc.h`); it also sets reaction time, the plan's main risk.
- [ ] **Bank 0 headroom.** Bank 0 code ends at `$3F6B` (about 150 bytes free). Move `player.c` or `river.c` into bank 2 or 3 before the HUD or sound ports land.
- [ ] **Sprite Y may be 1 px off** (NES draws at `y+1`). Check against the Atari after the jet is lowered.
- [ ] **Rework the HUD (separate, later task).** `src/gbc/hud_gbc.c` is a minimal stand-in (score, fuel bar, lives, bridge number). Needs a proper layout and the original charset art; also HI score, blink, game number.
- [ ] **Remove two-player mode for GBC.** Strip `pl_two`, `player_swap_players`, the `joypad_ex` path and the `hud_two`/`hud_other_bcd` state from the GBC build; the attract menu then only picks the level.

