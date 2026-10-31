# BUGS
- [x] Tanks shots are scrolling with the river (fixed 2026-10-30: the shell scrolls like every entity, as on the Atari)
- [ ] baloon basket color - should be brown
- [~] White striped helicopters do not shoot (ported 2026-10-31, awaiting playtest: the fast helicopter, types - [ ] White striped helicopters do not shoot (the Atari disassembly has no helicopter shell; the shooter is the side tank below, which only spawns beside a ship, helicopter or fuel depot at level 7+. Re-check after the side tank is play-tested)A/- [ ] White striped helicopters do not shoot (the Atari disassembly has no helicopter shell; the shooter is the side tank below, which only spawns beside a ship, helicopter or fuel depot at level 7+. Re-check after the side tank is play-tested)B, level 13+, fires a 4x1 px missile at 2 px/frame that dies on a bank or island outline and kills the jet; start at bridge 20 or 50. Sound is an approximation of the Atari ch2 zap)
- [~] There are no tanks shooting from side of the river (ported 2026-10-31, awaiting playtest: start at bridge 20 or 50 and wait; they sit on a bank or island, walk to the outline, then fire)
- [x] (fixed and play-tested 2026-10-31: `river_start` derives land colour from the bridge counter parity) Starting game. Level 1 is dark green grass. Get to level 2 - light green grass -> die -> Level 2 has now dark green grass. 
- [ ] Can't get to pause menu until plane starts moving
- [ ] Lives logic. Life (1 plane on the hud) is substracted only after river scrolls down fully and plane appears.y
- [ ] Level 50 tight bottle neck is practically unbeatable on NES. It is beatable on Atari.
- [ ] Level 20 has some weird glitch on the right side of the fork, near the ballon. It is straight river edge on atari but look like glitced in our case. It does not collide with the player, however. It is only visual. It might be some kind of river generator problem or bug.
- [ ] On heigher levels it seems like white horizontal flying planes get replaced by random enemies. And ships and helicopters fly across the river horizontally.

# FEATURES
- [ ] Atari 2600 mode, which would disable river edge tiles (Would it grant significant performance?)