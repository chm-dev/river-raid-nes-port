# BUGS
- [x] Tanks shots are scrolling with the river (fixed 2026-10-30: the shell scrolls like every entity, as on the Atari)
- [ ] baloon basket color - should be brown
- [x] White striped helicopters do not shoot (fixed and play-tested 2026-10-31: the fast helicopter, types $0A/$0B at level 13+, fires an M0 missile, a 4x1 px line at 2 px/frame that dies on a bank or island outline and kills the jet; noise-channel zap, silent in attract)
- [x] There are no tanks shooting from side of the river (fixed and play-tested 2026-10-31: `ent_side_tank_spawn`, a tank on a bank or island that walks to the outline and fires, level 7+)
- [x] Starting game. Level 1 is dark green grass. Get to level 2 - light green grass -> die -> Level 2 has now dark green grass. (fixed and play-tested 2026-10-31: `river_start` derives land colour from the bridge counter parity) 
- [ ] Can't get to pause menu until plane starts moving
- [ ] Lives logic. Life (1 plane on the hud) is substracted only after river scrolls down fully and plane appears.y
- [ ] Level 50 tight bottle neck is practically unbeatable on NES. It is beatable on Atari. I think colisionbox is smaller for atari plane. I feel like wings or parts of it are not included in the hitbox.
- [ ] Level 20 has some weird glitch on the right side of the fork, near the ballon. The biggest clue is the fact that it is straight vertical portion of the river edge on atari but looks  glitced in our case. It does not collide with the player, however. You can fly past it even though it looks like land. It is only visual. It is most likely some kind of river generator problem or bug becaue it can be observed in other levels too.
- [?] STALE, ignore unless seen again (2026-10-31, user cannot reproduce; no code path found, see PROGRESS.md "Plane replaced by random enemies"): On heigher levels (ie 50 and up but maybe earlier too) it seems like white horizontal flying planes get replaced by random enemies. And ships and helicopters fly across the river horizontally.
- [ ] Extra life sfx is played every 1000 points or so, whereas it should be playing only on extra life. Check how much points gives extra life and verify if it is implemented correctly.

# FEATURES
- [ ] Atari 2600 mode, which would disable river edge tiles (Would it grant significant performance?)
