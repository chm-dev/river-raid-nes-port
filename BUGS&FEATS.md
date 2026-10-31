# BUGS
- [x] Tanks shots are scrolling with the river (fixed 2026-10-30: the shell scrolls like every entity, as on the Atari)
- [ ] baloon basket color - should be brown
- [x] White striped helicopters do not shoot (fixed and play-tested 2026-10-31: the fast helicopter, types $0A/$0B at level 13+, fires an M0 missile, a 4x1 px line at 2 px/frame that dies on a bank or island outline and kills the jet; noise-channel zap, silent in attract)
- [x] There are no tanks shooting from side of the river (fixed and play-tested 2026-10-31: `ent_side_tank_spawn`, a tank on a bank or island that walks to the outline and fires, level 7+)
- [x] Starting game. Level 1 is dark green grass. Get to level 2 - light green grass -> die -> Level 2 has now dark green grass. (fixed and play-tested 2026-10-31: `river_start` derives land colour from the bridge counter parity) 
- [x] Can't get to pause menu until plane starts moving (fixed and play-tested 2026-10-31: START was only read in PLAY; the READY state now enters the pause menu too and RESUME returns to READY with the world still held)
- [x] Lives logic. Life (1 plane on the hud) is substracted only after river scrolls down fully and plane appears. (fixed and play-tested 2026-10-31: game starts with 4 icons, `hud_lives--` moved from `player_crash` to the WAIT->READY jet appearance; game over = crash with 0 in reserve)
- [ ] Level 50 tight bottle neck is practically unbeatable on NES. It is beatable on Atari. I think colisionbox is smaller for atari plane. I feel like wings or parts of it are not included in the hitbox.
- [ ] Level 20 has some weird glitch on the right side of the fork, near the ballon. The biggest clue is the fact that it is straight vertical portion of the river edge on atari but looks  glitced in our case. It does not collide with the player, however. You can fly past it even though it looks like land. It is only visual. It is most likely some kind of river generator problem or bug becaue it can be observed in other levels too.
- [?] STALE, ignore unless seen again (2026-10-31, user cannot reproduce; no code path found, see PROGRESS.md "Plane replaced by random enemies"): On heigher levels (ie 50 and up but maybe earlier too) it seems like white horizontal flying planes get replaced by random enemies. And ships and helicopters fly across the river horizontally.
- [x] Extra life sfx is played every 1000 points or so, whereas it should be playing only on extra life. (fixed and play-tested 2026-10-31: `entities_add_score` watched the thousands nibble `bcd[1]>>4` instead of the ten-thousands nibble `bcd[0]&0x0F`, so it granted a life + jingle every 1000 pts; now every 10,000. Lives icons are now BG tiles on HUD row 3 (up to 8, as on the Atari) because the gauge sprites already fill the 8/scanline limit. Play-tested with a 9-lives ROM 2026-10-31)

# FEATURES
- [ ] Atari 2600 mode, which would disable river edge tiles (Would it grant significant performance?)
