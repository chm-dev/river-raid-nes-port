# BUGS & FEATS — GBC port

Status: `[ ]` open · `[~]` fixed, waiting for the user's playtest · `[x]` confirmed in play by the user.
The NES list is in `archive/nes/BUGS&FEATS.md`.

# BUGS
Found in the milestone 1 playtest (`roms/m1_jet.gb`, 2026-10-31). All expected at this stage; not blocking.
- [ ] **Player shot is invisible** (it still destroys enemies). Likely the shot sprite's tile or palette; sprites are not yet set up for GBC. See `player.c` shot slot 47 and `entities_shot_tile()`.
- [ ] **Enemy sprites are black** (size and direction are right). Entity palettes still hold NES values; `tools/gen_entity_gfx.py` needs GBC palette output, then `set_sprite_palette` for them. Milestone 3 colour work, earlier if cheap.
- [ ] **Some side hills and trees glitch.** Most are fine. Not diagnosed; suspects: the unsplit tile job and the direct upload path writing while the display is on, or margin handling now that `RIVER_LEFT_COL` is 0. Check with `tests/compare_tile_rows.mjs` / `tests/host_river.ps1` before guessing.
- [ ] **Occasional stutter at fast speed.** Smooth otherwise and acceptable. Expected: `river_run_job` is not split across frames yet (plan: columns-per-frame limit). Measure before fixing.

# FEATURES
- [ ] **Move the jet lower on screen.** The 128 px view anchored at logic scanline 112 leaves the jet too far from the bottom and the playfield too short ahead of it. Tune `GBC_VIEW_TOP_SCANLINE` / the jet's Y (`config_gbc.h`); it also sets reaction time, the plan's main risk.
- [ ] **Bank 0 headroom.** Bank 0 code ends at `$3F6B` (about 150 bytes free). Move `player.c` or `river.c` into bank 2 or 3 before the HUD or sound ports land.
- [ ] **Sprite Y may be 1 px off** (NES draws at `y+1`). Check against the Atari after the jet is lowered.
