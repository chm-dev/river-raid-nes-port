# BUGS & FEATS — GBC port

Status: `[ ]` open · `[~]` fixed, waiting for the user's playtest · `[x]` confirmed in play by the user.
The NES list is in `archive/nes/BUGS&FEATS.md`.

# BUGS
Found in the milestone 1 playtest (`roms/m1_jet.gb`, 2026-10-31). All expected at this stage; not blocking.
- [x] **Player shot is invisible** (fixed in m2_hud: logic OAM had 40 slots, the shot is slot 47; now 48 packed into 40) - was: (it still destroys enemies). Likely the shot sprite's tile or palette; sprites are not yet set up for GBC. See `player.c` shot slot 47 and `entities_shot_tile()`.
- [x] **Enemy sprites are black** (m2_hud sets RGB sprite palettes in `gbc/main.c`; no generator change yet) - was: (size and direction are right). Entity palettes still hold NES values; `tools/gen_entity_gfx.py` needs GBC palette output, then `set_sprite_palette` for them. Milestone 3 colour work, earlier if cheap.
- [x] **Shots destroy enemies with a delay.** (fix built: `player_update_shot` now steps and tests the shot once per elapsed frame, 6 px each, instead of one 12 px jump; confirm in play.) Seen in the m2 playtest (shot and enemy colours confirmed fine): usually feels off, sometimes fine, as if the hit logic lagged the visuals. Suspect the 30 Hz logic (player/shot/collision every 2nd frame) against the 60 Hz scroll and sprite draw; the shot moves 6 px x ticks per logic pass, so it jumps 12 px and may be tested a frame after it is drawn. Check the shot/entity order and whether ntities_hit_shot uses the same positions that are drawn before changing LOGIC_FRAMES.
- [x] **Some side hills and trees glitch.** No longer reproduces (2026-11-01): it must have been fixed by some earlier change; the cause was never found. `tests/host_river.ps1` passes on all four start levels (the host stub `tests/host/gb/gb.h` was added to make it build again). Was: not diagnosed; suspects: the unsplit tile job and the direct upload path writing while the display is on, or margin handling now that `RIVER_LEFT_COL` is 0. Check with `tests/compare_tile_rows.mjs` / `tests/host_river.ps1` before guessing.
- [~] **Stutter at fast speed.** Measured: the loop ran at ~30 Hz at fast speed (0.51 passes per frame), mostly from `oam_camera` (31k units per pass), not only the tile job. Fixed `oam_camera` (8-bit math) and split `river_run_job` (6 non-solid columns per pass); now 0.74 passes per frame, cruise 1.00. Still not 60 Hz at fast: the player/entity groups and a 3-row generation pass overrun some frames. Check by eye at fast speed.
- [x] **Random entities remain and appear on screen** (suspected cause, fix built: on overrun passes the main loop always took the player branch and `entities_update` starved; now it runs at least every 2nd pass. Same for the next two.)  
- [x] **Death animation gets very slow when scene becomes busy**
- [x] **Enemies move very slow and stutter a lot**

# FEATURES
- [~] **Move the jet lower on screen.** The view is bottom-anchored (`GBC_VIEW_TOP_SCANLINE` 112), so only the jet's logic Y can move it: `PLAYER_START_Y` is 222 on GBC (was 218; sprites at y >= 232 are culled, so 222 is the limit). It also sets reaction time, the plan's main risk. If 4 px is not enough, thin the HUD Window (16 px) for look-ahead.
- [x] **Bank 0 headroom.** Done: `player.c` moved to bank 2; bank 0 now has ~2.6 KB free (see HANDOFF).
- [ ] **Sprite Y may be 1 px off** (NES draws at `y+1`). Check against the Atari after the jet is lowered.
- [ ] **Rework the HUD (separate, later task).** `src/gbc/hud_gbc.c` is a minimal stand-in (score, fuel bar, lives, bridge number). Needs a proper layout and the original charset art; also HI score, blink, game number.
- [ ] **Remove two-player mode for GBC.** Strip `pl_two`, `player_swap_players`, the `joypad_ex` path and the `hud_two`/`hud_other_bcd` state from the GBC build; the attract menu then only picks the level.

