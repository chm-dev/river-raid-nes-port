# Atari 8-Bit *River Raid* — Reverse-Engineering Specification

Phase 1 deliverable for the NES port. Every address below is in the Atari
6502 address space unless prefixed with `file+`.

## 0. Source and checksums

| Item | Value |
|---|---|
| Input | `xex/River Raid (1983)(Activision)(US)[a3].xex` |
| XEX size | 8,220 bytes |
| Cartridge slice | `$A000-$BFFF`, 8,192 bytes, unbanked |
| Cart SHA-256 | `54cd609164064ab89da72b82e820819bb1eb81ebf80fb208108cc093e7bc63ed` |
| XEX SHA-256 | `e30c9d838f45ca1f195c0d73dda358c43d8ca548485333fedc3976710574d613` |
| Loader stub | `$9FF0-$9FFF`: `RAMTOP=$60`, `RUNAD=$9FF0`, jumps to `$A000` |
| Screen | ANTIC mode E, 48 bytes/row, 157 visible rows, DMACTL `$3F` |

The machine used for all captures is an Altirra 800XL, 320 KiB, NTSC,
`NTSC` palette, BASIC off. The palette is exported verbatim to
`assets/atari/palette_altirra_ntsc.bin` (768 bytes, 256 RGB triplets).

## 1. Cartridge layout

| Range | Contents |
|---|---|
| `$A000-$A15C` | Cold init: hardware clear, display-list build, charset copy/patch, PMBASE and vectors. |
| `$A15D-$A196` | Restart entry: clears `$00-$09`, calls `$B0F1` (game init), `$AF80` (level init). |
| `$A197-$A270` | Main loop, console/joystick sampling, state dispatch on `$24`. |
| `$A271-$A2F4` | Object pass 1: remap, patrol X movement, wrap at `$2C/$D4`. |
| `$A2F5-$A3E8` | Player control, throttle, level/scroll gating. |
| `$A3E9-$A5B6` | Collisions, score BCD add, refuel, bridge/checkpoint, explosion start. |
| `$A5B7-$A6D2` | Shot launch, object-vs-object/terrain tests, console handling. |
| `$A6D3-$A9D6` | River row request, feature selection, scroll state. |
| `$A9D7-$AA79` | Display-list row pointer scroll and seam patching. |
| `$AA7A-$AB7F` | New 48-byte river row composition. |
| `$AB80-$AE98` | Counters, object aging, spawn placement. |
| `$AE99-$AFCF` | HUD/score/fuel updates, collision map writes. |
| `$AFD0-$AFEB` | Land colour / hit-flash (`COLPF0`). |
| `$AFEC-$B0E8` | Row fill primitives and seam templates. |
| `$B0E9-$B0F0` | Display-list seam patch templates (`30 00 4E 0E`, `20 00 4E 0E`). |
| `$B0F1-$B182` | Per-game init: clear RAM, PMG, object arrays, fuel gauge, colours. |
| `$B183-$B193` | 24-bit LFSR step. |
| `$B194-$B1BF` | Level advance / seed rotation. |
| `$B1C0-$B220` | Level-change and explosion transition. |
| `$B221-$B246` | Clear shot column / object row. |
| `$B247-$B260` | Swap 1P/2P state block (`$60-$75`). |
| `$B261-$B297` | High-score compare/copy. |
| `$B298-$B2A4` | High-score checksum. |
| `$B2A5-$B484` | Deferred VBI: sound engine, marquee scroll, colours, NMI/DMA re-arm. |
| `$B485-$B48C` | IRQ stub (POKEY IRQs off, `RTI`). |
| `$B48D-$B4FF` | Sound/flash/spawn tables. |
| `$B500-$B773` | DLI part 1 (playfield sprites) and part 2 (HUD/marquee). |
| `$B776-$B8D6` | Display-list templates, text, marquee text. |
| `$B8E0-$B9D5` | PMG shape data. |
| `$BA00-$BB0A` | More shapes, player turn shapes. |
| `$BB0B-$BB6F` | Score/spawn/remap/colour/shape/offset tables. |
| `$BB70-$BBAB` | HUD seed text. |
| `$BBB2-$BDFF` | River generation tables and 48-byte row templates. |
| `$BEEE-$BFFF` | Charset patch blocks, checksum data, vectors/unused. |

## 2. Memory map

### 2.1 Zero-page game state

| Addr | Name | Meaning |
|---|---|---|
| `$09` | skill | 0..7; selects one of four level-descriptor pairs (`$09>>1`). |
| `$0B` | `COLPF0` shadow | Land colour: `$D2` (dark green) or `$D6` (bright green). |
| `$0C-$16` | object Y | 11 bytes, one per object slot. Pixel Y + 75 mapping (see DLI). |
| `$18` | frame counter | Increments every VBI. Low bits drive animations. |
| `$19` | joystick | Raw PORTA bits (active low). |
| `$1A-$22` | collision accumulators | Set to `$FF` at DLI start, hold the last object index that collided. |
| `$23` | land/state flags | bit 2 = land brightness; bit 7 = alive/game-over; bit 4 = ? |
| `$24` | debounce | 3-frame input/transition counter. |
| `$25` | skill countdown | Frames until the attract skill level changes. |
| `$26` | marquee fine scroll | HSCROL value for the marquee row (0..7). |
| `$27` | marquee coarse scroll | Low byte of the marquee text pointer (`$B8xx`). |
| `$28` | game number | 1 or 2 (2P mode). |
| `$29` | mode flags | bit 7 = demo/attract, bit 6 = paused, low bits = counters |
| `$2A` | sound/channel lock | Nonzero disables some HUD updates and shot sounds. |
| `$2B` | player X velocity | Sub-pixel delta applied to `$58`. |
| `$2C` | object X min | Left patrol bound / island margin. |
| `$2D` | river flags | bit 7 = fork/island active, bit 6 = alternate bank, bit 5 = ? |
| `$2E` | section counter | Incremented at each bridge (checkpoint). |
| `$2F` | row counter | Row index within the current level section; compared with `$BE`. |
| `$30` | score delta | Temporary score/section value. |
| `$31` | flash timer | Bridge/explosion flash countdown. |
| `$32` | shape offset temp | `$BB60[aux] + 4`. |
| `$33` | bank toggle | Toggles each level; affects `$B1` (collision map parity). |
| `$34` | row end | End row for the current feature (`BFENLO`). |
| `$36` | feature flags | bit 7 = feature/bridge/fork direction; 0 = none. |
| `$37` | direction flag | River/feature direction. |
| `$38` | fuel/refuel flag | bit 7 = refuelling; low bits = object shape state. |
| `$39` | P1/shot X | `HPOSP1`; also shot X anchor. |
| `$3A` | shot timer | Frames since the last shot. |
| `$3B` | row pattern index | Index into `$BAB7` / `$B801` patterns. |
| `$3C` | P0 X | `HPOSP0`; also player shot coarse X. |
| `$3D-$3F` | shot X/Y | 16-bit-ish sub-pixel shot position. |
| `$40` | scroll counter | Rows scrolled since level start; wraps at `$BC`. |
| `$41` | sound flags | bit 7 = engine/shot active. |
| `$42-$4C` | object status | 11 bytes; bit 7 = active, bit 3 = direction, bit 2 = ? |
| `$4D` | object slot count | Number of active object entries (0..11). Also DLI start index. |
| `$4E` | max object slot | Level/feature dependent cap. |
| `$4F` | hit flash | Nonzero draws edge `$AA` instead of land `$55`. |
| `$50` | shot animation | `$FF` = idle, 0..$38 = shot phase/frequency index. |
| `$51` | refuel slot | Object slot being refuelled/claimed. |
| `$52` | left bank X | Pixel X of the left river bank (0..191). |
| `$53` | right bank X | Pixel X of the right river bank (0..191). |
| `$54` | row phase | Row scroll phase used by the bank generator. |
| `$55` | P2 X? | Reserved/unused in 1P. |
| `$56` | player X coarse | Player column used for shot launch. |
| `$57` | player X | `HPOSP2`; the jet's horizontal position. |
| `$58` | player X fine | Sub-pixel accumulator. |
| `$59` | shot Y fine? | Used by shot/player animation. |
| `$5A` | row timer | Counts down to the next feature row. |
| `$5B/$5C` | hit/explosion counter | 16-bit counter; low byte drives sound. |
| `$5D` | level countdown | 16..1; reaches 1 → advance level/section. |
| `$5E` | player shape | 7/8/9 = bank left / bank right / straight; other values reuse object shapes. |
| `$5F` | island width | Width/margin of the current fork. |
| `$60` | level | 1..$50; incremented at each section. |
| `$61/$62` | BCD distance | 2-byte BCD counter (section/distance). |
| `$63-$65` | score | 3-byte BCD, displayed as six digits. |
| `$66-$69` | next seeds | LFSR seeds and margin for the next level. |
| `$6A` | lives | Remaining lives. |
| `$6B-$75` | 2P mirror | Second-player copy of `$60-$69` variables. |
| `$76` | throttle | `$FF` = stopped, `0` = maximum speed. |
| `$77` | control mode | 0 = 1P, 1 = 2P. |
| `$78` | attract timer | Incremented every 8 frames in attract. |
| `$79` | key repeat | Nonzero = joystick held. |
| `$7A/$7B` | engine sound | `$7A` = pitch increment, `$7B` = sweep counter. |
| `$7C` | explosion sound | Timer; random noise on AUDF2. |
| `$7D` | shot sound 3 | Timer; frequency sweep on AUDF3. |
| `$7E` | beep/explosion 3 | Timer; frequency from `$B4E1`. |
| `$7F` | enemy sound | Timer; tone on AUDF2. |
| `$80-$8E` | high scores | 15 bytes. |
| `$90-$9F` | high-score checksums | 16 bytes. |
| `$A0` | checksum salt | `$0F` at boot. |
| `$A1/$A2` | source pointer | Row generator source. |
| `$A3/$A4` | destination pointer | Current row address in the playfield ring. |
| `$A5/$A6` | DL seam pointers | Insert positions for the two `LMS` seams. |
| `$A7/$A8` | DL patch flags | Nonzero applies a seam patch. |
| `$AA-$AC` | LFSR state | 24-bit shift register. |
| `$AD-$B0` | previous seeds | Saved `$66-$69` at level advance. |
| `$B1` | collision-map parity | `$90` or `$d0` base for HUD/score columns. |
| `$B4` | row byte offset | Accumulated destination offset. |
| `$B5` | island width | Current fork/island width. |
| `$B6` | fill counter | Temp for fill primitives. |
| `$B7` | temp | Bank/edge computation. |
| `$B9` | player X direction | Sign bit used by movement. |
| `$BA/$BB` | template pointer | 16-bit pointer into the `$BCxx` row templates. |
| `$BC` | template length | Bytes to copy from the template. |
| `$BD` | feature/object type | Feature shape/aux byte. |
| `$BE` | feature end row | Row count/limit for the feature. |
| `$BF` | NTSC/PAL flag | Read from `COLPM2 & $0E` at init; 0 on PAL. |
| `$C0` | shot X sign | `+2` or `-2`. |
| `$C1` | shot X offset | Current shot sprite X offset. |
| `$C2` | explosion slot | Object index of the current explosion. |
| `$C3-$C9` | river temps | Direction, bank/width temps. |
| `$CA-$F6` | sound/OS scratch | POKEY shadow and temp values. |

### 2.2 Object arrays (11 slots, `X = 0..10`)

| Array | Address | Meaning |
|---|---|---|
| type | `$0500+X` | Object type: 0 = player/dummy, 1 = moving enemy, 2 = explosion, 7..$0E = patrol/terrain, $0F = fuel/bridge. |
| aux | `$050B+X` | Shape/colour index into `$BB30/$BB40/$BB60`; also used as score index. |
| X | `$0516+X` | Pixel X (PMG units; `HPOSP3`). |
| flags | `$0521+X` | bit 7 = direction, low bits = object flags (e.g. $80 = hit flash). |
| Y | `$000C+X` | Pixel Y + 75 (see §4.4). |
| status | `$0042+X` | bit 7 = active, bit 3 = direction, bit 2 = ?. |

Slot 10 is also used as the bridge/fuel sentinel and as the object that finds
the next free spawn slot. `$4D` is the active count and the DLI's starting
index; `$4E` is the level-dependent cap (usually 11).

### 2.3 Screen memory

| Range | Contents |
|---|---|
| `$1000-$103B` | HUD text rows (3 × 20 bytes, mode 6). |
| `$2000-$2EFF` | River ring half 0 (80 rows × 48 bytes). |
| `$2F00-$2FFF` | **Skipped** by the DL and row generator. |
| `$3000-$3EFF` | River ring half 1 (80 rows × 48 bytes). |
| `$3F00-$3FBF` | Display list. |
| `$0600-$09FF` | Character set (4 × 256 bytes, rearranged pages). |
| `$0C00-$0CFF` | Player 0 graphics (unused in 1P). |
| `$0D00-$0DFF` | Player 1 graphics (unused/legacy). |
| `$0E00-$0EFF` | Player 2 graphics: player jet at `$0EAA-$0EB7`, fuel gauge at `$0EDB-$0EE1`. |
| `$0F00-$0FFF` | Player 3 graphics: multiplexed enemy shapes written by the DLI at `$0EBC+X`. |
| `$0B00-$0BFF` | Missile graphics region (all four missiles; fuel gauge copy at `$0BCC-$0BD2`). |
| `$0A00-$0AFF` | Unused PMG page. |

PMBASE is `$08`. The GTIA mapping verified on a bare machine is
`P0=$0C00`, `P1=$0D00`, `P2=$0E00`, `P3=$0F00`; missiles occupy
`PMBASE+$0300` (`$0B00`).

## 3. Display architecture

### 3.1 Display list

Built once at `$3F00` and patched at the seams. Runtime shape:

| Addr | Instruction | Meaning |
|---|---|---|
| `$3F00` | `$70` | 8 blank lines |
| `$3F01` | `$F0` | 8 blank lines, DLI at the end (scanline 16) |
| `$3F02` | `$30` | 4 blank lines |
| `$3F03` | `$4E lo hi` | LMS + mode E; first visible row (scroll pointer) |
| `$3F06` | `$0E` × N1 | mode E rows |
| `$3F28`-ish | `$4E 00 30` | LMS + mode E → `$3000` |
| | `$0E` × N2 | mode E rows |
| `$3F7A`-ish | `$4E 00 20` | LMS + mode E → `$2000` |
| | `$0E` × N3 | mode E rows |
| `$3FA9` | `$10` | 2 blank lines |
| `$3FAA` | `$46 00 10` | LMS + mode 6 → `$1000` (HUD row 0, 20 bytes/line) |
| `$3FAD` | `$00` | 1 blank line |
| `$3FAE` | `$06` | mode 6 → `$1014` (HUD row 1) |
| `$3FAF` | `$06` | mode 6 → `$1028` (HUD row 2) |
| `$3FB0` | `$A0` | 3 blank lines + DLI (scanline ~196) |
| `$3FB1` | `$00` | 1 blank line |
| `$3FB2` | `$56 e7 b8` | LMS + hscroll + mode 6 → marquee text |
| `$3FB5` | `$41 00 3f` | JVB |

The HUD rows run with `DMACTL=$3E` (standard width), so mode 6 fetches
20 bytes per row and the rows advance by `$14`. DLI part 2 switches to
`DMACTL=$3F` for the wide marquee row.

The three mode-E segments always total **157 lines**. At the captured
gameplay frame the segments were:

| Segment | LMS | Lines |
|---|---|---|
| 0 | `$2870` | 34 |
| 1 | `$3000` | 79 |
| 2 | `$2000` | 44 |

The 160-row ring is therefore displayed with three hidden rows at the
seams (one at each segment boundary). At the captured frame those rows are
`$2840`, `$2ED0` and `$3ED0`; the set rotates as the pointer scrolls. The
row pointer is decremented by `$30` per scrolled row and wraps `$20..$3E`,
skipping page `$2F`.

### 3.2 Mode E geometry

* 48 bytes fetched per row (`DMACTL=$3F`, wide playfield); ANTIC stride `$30`.
* Each byte = 4 pixels, 2 bits/pixel.
* Pixel value → colour:

| Value | Register | Game colour |
|---|---|---|
| 0 | `COLBK` | `$00` black |
| 1 | `COLPF0` | `$D2`/`$D6` green (level parity) |
| 2 | `COLPF1` | `$20` dark red outline |
| 3 | `COLPF2` | `$84` blue water |

* The visible 40-byte window is bytes **4..43** of each 48-byte row. Bytes
  0..3 and 44..47 are horizontal overscan. A 336-pixel screenshot shows 42
  bytes (3..44) because it includes 8-pixel borders.
* Row bytes used by the generator: `$55` = land (01), `$FF` = water (11),
  `$AA` = solid edge (10), `$56/$AB`, `$6A/$BF`, `$5A/$AF`, `$FE/$A9`,
  `$EA/$95`, `$FA/$A5` = 2-byte bank transitions.

### 3.3 DLI part 1 — playfield sprites

Vector `VDSLST=$B500`. It runs at the `$F0` blank-line DLI (scanline 16).
If `VCOUNT >= $3F` it jumps to part 2 at `$B72A`.

Sequence:

1. `$B510-$B525`: clear collision accumulators `$1A-$22` to `$FF`.
2. `$B527-$B55A`: set P0/P1/P2/M0/M1/M2 positions and colours:
   * `HPOSP1 = $39`; `HPOSM1 = ($5B>>3)+$5C`.
   * `HPOSP2 = $57`; `HPOSM2 = $57+4`.
   * `COLPM1 = 0`.
   * `COLPM2 = $1E` in 1P, `$8E` in 2P.
   * `HPOSP0 = $3C`; `HPOSM0 = $C1`.
3. `$B55C`: start the multiplexed object loop at `X=$5D` (93), object index
   `Y=$4D`.
4. For each object, erase the two PMG bytes at the current beam position
   (`$0EBC+X`, `$0EBD+X`), then `WSYNC` until `X >= object_y`; then:
   * `HPOSP3 = object_x`.
   * Read `aux = $050B,Y`; `shape_ptr = $B900 + $BB40[aux]`,
     `shape_off = $BB60[aux]`, `colour_ptr = $B700 + $BB30[aux]`.
   * `start = X`; `X++`; then for `i = shape_off down to 0`:
     `$0EBC+X = shape[i]`, `COLPM3 = colour[i]`, `WSYNC`, `X++`.
   * Collision bits are accumulated into `$1A-$22` from `M0PL/M0PF/P1PF/P2PF/P2PL`.
5. When `X` wraps to 0, set `HPOSP0/1/HPOSM0 = 0`, `DMACTL=$3E`,
   `COLPM1=$1E`, `COLPF0=$1E`, `COLPF1=$8E`, `COLPF2=$8E`/`$1E`, then fall
   into the HUD/marquee code.

**Coordinate translation.** The DLI's `X` is the beam line counter and the
shape destination is `P3 offset = X + $BC (mod 256)`. GTIA displays PMG
offset `O` on scanline `O - 8`. Therefore:

```
screen_scanline = object_y - 75          (object_y in $5F..$FB visible)
object_y        = screen_scanline + 75
```

The first playfield line (scanline 20) is `object_y = 95`; the last
(scanline 176) is `object_y = 251`.

The object shape tables are stored **bottom-up**: index 0 is the bottom
row, index `$BB60[aux]` is the top row.

### 3.4 DLI part 2 — HUD and marquee

At `$B72A`, triggered by the `$A0` blank-line DLI (scanline ~196):

* `DMACTL=$3F`, `COLPF0=$0C`, `COLBK=$00`, `CHBASE=$06`.
* `SIZEP3=$03` (quad width).
* Marquee: `HPOSP3=$27`, `HPOSP2=$27+$15`; seven scanlines of colours from
  `$B48D..$B493` are written to `COLPM3/COLPM2`.
* `HITCLR` is strobed; `XITVBV` returns.

The marquee text pointer low byte is `$3FB3`, advanced by the deferred VBI
(`$B44B-$B47A`) with fine horizontal scroll in `HSCROL`. The pointer walks
the `$B84B-$B8D6` range and wraps to `$4B` after `$D6`. `$27` is the
marquee's `HPOSP3` (it decreases by one per VBI, moving the row left);
`$26` is the 0..7 fine counter that advances the text pointer every eight
frames. This gives a smooth pixel scroll instead of a character jump.

Source range is `$B84B-$B8D6` (credits text), which is decoded in
`assets/atari/extracted/tables.json` as `marquee_text`:

```
                          RIVER RAIDYZ by Carol Shaw    Copyright {|}zX{|}~
   [\]^_                        Press START key    Use joystick {
```

(`Y`, `Z`, `{|}` etc. are graphic glyph codes, not letters.)

### 3.5 Player jet

The 14-row P2 shape is copied every frame by `$AF22-$AF38`:

```
ptr = $BACD[$5E*2]         ; $5E = 7/8/9 fast bank-left/bank-right/straight
for Y = 13 down to 0: $0EAA+Y = ptr[Y]
```

`$0EAA` is P2 offset `$AA`; it displays on scanlines **162..175**.
Only `$5E = 7/8/9` are normal flight shapes (bank-left, bank-right,
straight). `$5E = 0/1` point at a zero page (the jet is invisible during the
start/respawn delay), and 2..6 reuse shared enemy/effect fragments, producing
debris-like frames during a crash transition. The jet's X is
`HPOSP2 = $57` (124 at the captured frame), giving screen X
`2*$57 - 88 = 160` in the 336-pixel frame (first lit pixel 162).

### 3.6 Fuel gauge

`$B133-$B15C` writes, at level init:

* `$0EDB+0..6 = $FF` (P2), `$0FDB+0..6 = $FC` (P3),
* `$0BCC+0..6 = $0C` (missile area),

at scanlines `$DB-8 = 211..217`. `$BF` (NTSC/PAL) selects the base offset
`6` vs `$12`; the PAL path moves the bar down 12 scanlines. The bar is
7 scanlines tall and 8 bits wide; the HUD reference crop shows the resulting
colour strip.

## 4. Colour registers

| Register | Value | Use |
|---|---|---|
| `COLBK` | `$00` | Black background. |
| `COLPF0` | `$D2`/`$D6` | Land green; `$AFD0` computes `(($23&4)^$D6)`. |
| `COLPF1` | `$20` | Dark red river outline / text. |
| `COLPF2` | `$84` | Blue water. |
| `COLPF3` | `$0C` | HUD text / marquee. |
| `COLPM0` | `$BAC5[(frame>>3)&7]` | Player-shot/legacy sprite; values `$1E,$2E,$3E,$4E,$6E,$7E,$9E,$BE`. |
| `COLPM1` | `$1E` playfield, `$00` HUD | Unused/legacy. |
| `COLPM2` | `$1E` (1P) / `$8E` (2P) | Player jet. |
| `COLPM3` | `$B7xx` strip | Multiplexed enemy shapes; marquee bars in HUD. |

`$AFD0` applies the hit flash: if `$4E < 11` and `object_status[Y] & $80`
and `object_y < $5C`, it XORs bit 2 into `COLPF0`.

### 4.1 Hardware registers used

| Chip | Registers |
|---|---|
| ANTIC `$D400-$D40F` | `DMACTL`, `DLISTL/H`, `HSCROL`, `VSCROL`, `PMBASE`, `CHBASE`, `NMIEN`, `VCOUNT`, `WSYNC`. |
| GTIA `$D000-$D01F` | `HPOSP0-3`, `HPOSM0-3`, `SIZEP0-3`, `SIZEM`, `COLPM0-3`, `COLPF0-3`, `COLBK`, `PRIOR`, `GRACTL`, `HITCLR`, `CONSOL`, `TRIG0`, collision registers `M0PF/P1PF/P2PF/M0PL/P2PL/P3PL`. |
| POKEY `$D200-$D20F` | `AUDF1-4`, `AUDC1-4`, `AUDCTL` (not written), `SKCTL`, `RANDOM`, `IRQEN`, `SKSTAT`. |
| PIA `$D300-$D303` | `PORTA` (joystick), `PACTL`, `PBCTL`. |

## 5. Frame loop and mode flags

`$A197` is the main loop. It spins until `VCOUNT >= $50` (bottom of the
visible screen), then samples PORTA (joystick), TRIG0 (fire), and CONSOL
(start/select), and updates the mode/transition counters.

* `$29` selects the mode: **bit 7 = attract/demo**, **bit 6 = paused**;
  `$29 = 0` during normal play.
* `$24` is a 3-frame input/transition debounce counter. When it is zero the
  code arms it to 3 and takes the HUD-only path; the next frames decrement
  it. It is not the main game-state variable.
* `$50` is the shot animation/frequency counter: `$FF` = idle, `$00-$38` =
  active shot phase. It is not a game-phase flag.
* `$76` is the throttle: `$FF` = stopped, `$00` = maximum speed.
* `$5E` selects the jet shape: 7 = bank-left, 8 = bank-right, 9 = straight;
  0/1 are blank (start/respawn) and 2..6 are shared effect fragments.

Per frame in play:

1. Player input → `$57/$58` X, `$76` throttle, `$5E` bank shape.
2. If `$76` allows, decrement the scroll delay and call the row generator
   when a new row is due.
3. Object pass (`$A271`): remap types, move X within patrol bounds, wrap at
   `$2C..$D4`.
4. Spawn/age (`$AB80`): increment the Y array, insert new objects every 256
   rows, age/expire objects, select types from `$BB1C`.
5. Collisions (`$A3E9`, `$A5B7`): player/shot vs terrain, objects, fuel,
   bridge; add BCD score; set explosion; advance section.
6. HUD (`$AE99`): write score/fuel/bridge counters into `$1000`.

Attract mode (`$29 bit7 = 1`) uses a demo input source, cycles the skill
level every `$25` frames, and waits for START. START calls `$A15D` to begin
a new game.

## 6. River generation

### 6.1 Model

The river is a 160-row ring of 48-byte mode-E rows. The visible window is
157 rows; three seam rows are hidden by the DL. Scrolling moves the DL row
pointer backwards one row at a time; the newly exposed row is generated at
the top of the ring.

Per row, the generator maintains:

| Variable | Meaning |
|---|---|
| `$52` | left bank pixel X (0..191) |
| `$53` | right bank pixel X (0..191) |
| `$54` | row phase / progression |
| `$36` | feature flags |
| `$2D` | fork/island flags |
| `$5F` | island width |
| `$2C` | island margin |
| `$5D` | level countdown |
| `$AA-$AC` | LFSR state |
| `$66-$69` | next level seeds/margin |

The LFSR at `$B183` is:

```
A = $AB
A <<= 5
A ^= $AB
A <<= 1
ROL $AA
ROL $AC
ROL $AB
```

`$B194` advances the level: saves `$66-$69` to `$AD-$B0`, copies
`$AA-$AC` into `$66-$68`, sets `$69=$52`, increments `$60`.

### 6.2 Row composition

The row is built left-to-right by these primitives:

| Routine | Action |
|---|---|
| `$AFF4` | Fill with `$55` land, unless `$4F` (hit flash) → `$AA`. Advances dest by `(X&3)*4`. |
| `$AFF0` | Fill with `$FF` water (or `$AA` if `$4F`). Advances by `(X&3)*4`. |
| `$AFEC` | Fill with `$AA` edge. |
| `$B00F` | Copy `Y+1` bytes from `($A1)` to the row, no advance (overlay object/fuel/bridge). |
| `$B01A` | Copy `$BC` bytes from `($BA)` to the row, advance both and `$B4`. |
| `$B034` | Add `$10` when the hit-flash variant is active, then fall into `$B03B`. |
| `$B03B` | Write the 2-byte bank transition from `$B800+A`; advance dest 2, `$B4 += 2`. |
| `$B04B` | Advance dest by `A`, `$B4 += A`. |

The bank table at `$B800` is 16 bytes and is indexed in 4-byte groups:

| Index | Left edge | Index | Right edge |
|---|---|---|---|
| 0 | `AA FF` | 2 | `AA 55` |
| 4 | `6A BF` | 6 | `EA 95` |
| 8 | `5A AF` | 10 | `FA A5` |
| 12 | `56 AB` | 14 | `FE A9` |

A second 16-byte table at `$B810` holds the corresponding island/fork edges
(`AA FF`, `AA AA`, `AA BF`, `EA AA`, `AA AF`, `FA AA`, `AA AB`, `FE AA`).
The generator adds `$10` to the pattern index when drawing an island with
`ROWCRS < 8`.

`A = (X & 3) * 4` selects the pixel phase within the 4-pixel byte; the
right edge uses `A + 2`.

The canonical vertical channel row is:

```
55 ... 55 | 6A BF | FF ... FF | EA 95 | 55 ... 55
```

A flat/straight row uses `AA FF` / `AA 55` instead.

### 6.3 Feature selection (`$A9A4-$A9D5`)

Per-row control flow in `$A6D3-$A9D6`:

1. `$A72B-$A744`: clear `$2F/$36/$052D`, set the land/state flag in `$23`,
   set `$2C=$5F`, decrement `$5A`. When `$5A` reaches 0, call `$B194`
   (level advance) and reset `$5D=16`.
2. `$A756`: step the LFSR. The level parity decides whether the feature
   index comes from the LFSR low 3 bits or from `$5F/$2C`.
3. `$A84F-$A887`: choose the template width/length for the current level
   and row (`$C4/$C5`).
4. `$A889-$A8D2`: the LFSR low nibble sets direction and the bank targets
   `$C3/$C4/$C5/$C8/$C9`.
5. `$A8DC-$A95D`: clamp the left/right banks to the bank margins; the
   direction bit in `$C3` selects the expanding or contracting side.
6. `$A96C-$A9D5`: when the LFSR bit 5 is set and `$2F==0`, select a
   feature template; otherwise the row is a normal channel.

At feature rows the generator selects a template:

```
if LFSR bit 5 ($AC & $20):
    if level is odd:
        idx = $BBCC[LFSR & 7]
    else:
        idx = $BBD4[min($5F, $2C)]
    $BD = idx                    ; feature/object type
    $BC = $BBB2[idx]             ; template width
    $BE = $BBEB[$BC]             ; feature length
    $36 = f($AC, $52)            ; direction/flag
    $BA/$BB = $BBF0 + $BC*2      ; 48-byte template pointer (first entry $BBF6)
```

The `$BCxx` data is a continuous stream of 48-byte row fragments; the
pointers at `$BBF6` select starting offsets (several overlap). `$B01A`
copies `$BC` bytes from the selected offset over the water, then the
generator continues with the banks. `$BBB2` widths, `$BBEB` lengths and
`$BBF6` pointers are all emitted in `assets/atari/extracted/tables.json`.

### 6.4 Rows reference

`assets/atari/extracted/river_rows_157.bin` is the captured 157×48 ring
from the gameplay frame; `river_reference.png` renders it with the Atari
palette. `river_state.json` records the exact DL segments and generator
state. These are the acceptance fixtures for Phase 4.

## 7. Objects

### 7.1 Types and shapes

`$BB24` maps incoming types to aux/shape indices:

```
00 01 03 04 05 06 00 07 09 08 0B 0A
```

`$BB1C` is the spawn type table:

```
0D 08 0D 08 07 0D 08 0C
```

Objects are inserted at slot `$4D` by shifting slots `$4D..9` upward one
slot (`$AB9A-$ABC8`). The new slot receives:

```
type   = selected spawn type
aux    = selected spawn type
X      = $BFE4[slot] adjusted by LFSR/edge rules
Y      = $61 - ($BB60[aux] + 4)
flags  = $BBA2[aux]
status = $42[slot]
```

The spawn block runs when object slot 10's Y wraps to 0 (every 256 rows).

### 7.2 Shape/aux table

| aux | rows | shape | colour strip | interpretation |
|---|---|---|---|---|
| 7 | 6 | `$B985` | `$B7C4` | small 6-row object (boat/helicopter; not yet classified) |
| 8, 10 | 10 | `$B98B` | `$B7B0`/`$B7BA` | plane (yellow/green) |
| 9, 11 | 10 | `$B995` | `$B7B0`/`$B7BA` | plane variant |
| 12 | 20 | `$B9D5` | `$B7E0` | explosion / large effect |
| 13 | 8 | `$B97D` | `$B7CA` | ship/boat (confirmed in gameplay) |
| 14 | 20 | `$B9C1` | `$B7CF` | explosion / large effect |
| 15 | 24 | `$B965` | `$B798` | FUEL depot (white/red letters) |

All P3 shapes are stored bottom-up and rendered with `COLPM3` set per
scanline from the colour strip. `assets/atari/extracted/sprites/objects.png`
shows every aux with its real colours.

### 7.3 Movement

* All 11 Y bytes (`$0C-$16`) increment by 1 per **scrolled row**, not per
  frame. This keeps objects locked to the river row coordinate.
* X movement: type `7..$E` patrols with `$0521` direction; wrap at
  `$2C..$D4` (`$A2D0-$A2EA`).
* Player X: `$57/$58` via joystick, clamped to the river.

### 7.4 Collision and scoring

* GTIA collision registers are sampled inside the DLI and accumulated into
  `$1A-$22`; the main loop maps them to object slots.
* `$BB0B` is a 17-entry BCD score table indexed by aux/type:
  `00 00 00 00 00 00 00 10 06 06 15 15 06 03 50 08 75`.
  Values are added to `$63-$65` with `SED`.
* Bridge/fuel hits increment `$61/$62` BCD and `$2E`; `$50` is the shot
  animation/frequency counter.
* The plan's expected 80/500 point awards still need a dynamic confirmation;
  the table above is the authoritative in-ROM data.

## 8. HUD

The HUD is three mode-6 rows of **20 characters** (ANTIC mode 6 fetches 20
bytes/line, 8 scanlines tall). The row buffers are 20 bytes apart:

| Row | Buffer | Contents |
|---|---|---|
| 0 | `$1000-$1013` | `P1`, status digits, high-score digit |
| 1 | `$1014-$1027` | `GAME`, `HI`, fuel gauge, score |
| 2 | `$1028-$103B` | `1`, `BRIDGE`, bridge icons |

* `$BB70-$BBAB` seeds the 60-byte template at `$1000-$103B` at boot.
* `$B05C` writes the 2-byte BCD counter `$61` at `$1000+$23` (base `$90`).
* `$B068` writes the 3-byte score `$63-$65` at `$1000+$0C` (base `$50`).
* `$B06E` writes the 3-byte score at `$1000+$0C` with base
  `$80 + ($09&$0E)*2` (skill display).
* `$B07C` writes the 3-byte score at `$1000+$02` (base `$10`).
* `$B064` writes the 2-byte counter `$61` at `$1000+$37` (base `$90`).
* Digit conversion: high nibble and low nibble each become `base + digit`.
  Codes `$10-$19` are digits when the base is `$10`.

The custom charset is a rearranged copy of the OS ROM:

| Game page | Copied from | Patch |
|---|---|---|
| `$0600` | OS `$E100` | `$06C0 ← $BEEE` (64 B) |
| `$0700` | OS `$E300` | `$07D0 ← $BFB6` (48 B) |
| `$0800` | OS `$E000` | `$0818 ← $BF2E` (112 B) |
| `$0900` | OS `$E100` | `$08D0 ← $BF9E` (24 B) |

The runtime 1 KiB copy is in `assets/atari/extracted/charset.bin`. Glyph
code `c` displays `charset[(c & 0x7F) * 8 .. +8]`.

### 8.1 HUD glyphs (use these directly)

The charset already contains the complete HUD artwork.  The small labels
use a compressed 4-pixel font (two letters per 8x8 glyph); the gauge and
plane icon are dedicated glyphs.  Note that the live HUD buffer stores the
high-bit forms (`$C7..$CE`), so mask with `$7F` before indexing.

| Code | Art | Use |
|---|---|---|
| `$43` / `$44` / `$45` / `$46` | "BR" / "ID" / "G" / "E" | `BRIDGE` (4 glyphs, doubled to 16 px each) |
| `$47` / `$48` / `$49` / `$4A` | gauge top row | assembled `47 48 49 48 4A` |
| `$4B` / `$4C` / `$4D` / `$4E` | gauge bottom row | assembled `4B 4C 4D 4C 4E` |
| `$5A` / `$5B` | "P1" / "HI" | player and high-score labels (doubled) |
| `$5C` | plane icon | remaining-lives icons |
| `$01-$1A` | uppercase A-Z | `GAME` (`G/A/M/E = $07/$01/$0D/$05`) |

The gauge's E, 1/2 and F are baked into `$47..$4E`; they are fixed labels,
not a fuel readout.  The level is the yellow vertical bar, which is 10 px
tall and 4 px wide, starts just below the top markers, and moves from the F
marker (full) to the E marker (empty); it is drawn behind the labels.  `$48`
carries the top rule, `$49` the upper half of 1/2, `$4C` the bottom rule and
`$4D` the lower half.  At mode-6 scale each glyph is 16 px wide, so the
5-cell gauge is 80 px wide.  See `assets/atari/extracted/README.md` for the
full asset/glyph map.

## 9. Audio

The deferred VBI at `$B2A5` drives four POKEY channels:

| Channel | Source | Notes |
|---|---|---|
| 1 `AUDF1/AUDC1` | `$C6` shot timer; otherwise `$7A/$7B/$76/$5B/$5C` engine + `$B4AC` volume table | `$C6` writes `AUDF1=$40`, `AUDC1=$AF`. |
| 2 `AUDF2/AUDC2` | `$7C` explosion noise; `$7F` enemy tone | Explosion uses `RANDOM | $18`; enemy frequency `$46+$7F`. |
| 3 `AUDF3/AUDC3` | `$7D` shot sweep; `$7E` beep/explosion | `$7E` frequency = `$B4E1[$7E>>4]`; `$7D` pitch from `(0x1C-$7D)*3`. |
| 4 `AUDF4/AUDC4` | `$50` shot frequency, else random | `$B4C0[shot_index]` with `AUDC4=$AC`; random `|$20` with `$8C`. |

Tables:

| Address | Length | Contents |
|---|---|---|
| `$B4AC` | 4 | `00 83 86 84` engine AUDC by joystick bits. |
| `$B4C0` | 18 | `0C 0E 11 15 19 1E 24 2B 34 3E 4A 59 6B 80 9A B9 DE 00`. |
| `$B4E1` | 4 | `30 40 50 60` FX frequencies. |
| `$B4E5` | 20 | `04 04 04 04 08 08 08 08 08 08 1C 1C 06...` flash colours. |

Sound triggers observed in the main code: shot `$C6=2`, explosion `$7C=$17/$37`,
refuel/enemy `$7F=9`, beep `$7E=$40`, shot sweep `$7D=$0F`.

## 10. Startup and high scores

`$A000` clears all hardware, sets up the DL and vectors, copies the OS
charset, applies patches, seeds the HUD, then:

```
$B298` checksum over $80-$8E, XOR salt $A0, compared with $90-$9F.
If mismatch -> reset high scores.
```

`$B0F1` copies 19 default bytes from `$BAE1` to `$4D-$5F`, clears the PMG
pages `$0C-$0F`, the river ring `$20-$3E`, object arrays and HUD, then sets
the fuel gauge, `$60` land colour, `$4D/$4E=10`, and the level descriptor.

`$AF80` initialises the level from `$B494-$B4AB`:

| Skill pair | seed0 | seed1 | seed2 | margin | start level | initial counter |
|---|---|---|---|---|---|---|
| 0 (skills 0/1) | E3 | D4 | F7 | 20 | 01 | 01 |
| 1 (skills 2/3) | 20 | 83 | B5 | 21 | 05 | 05 |
| 2 (skills 4/5) | 80 | C1 | 7F | 20 | 14 | 20 |
| 3 (skills 6/7) | B4 | 6D | C0 | 21 | 32 | 50 |

`$60 = start level`, `$62 = initial counter`, `$61 = 0`.

## 11. Subroutine catalog

The complete instruction-by-instruction listing is in
`docs/atari_disasm_a000_b2a4.txt`, `docs/atari_disasm_b2a5_b6ff.txt` and
`docs/atari_disasm_b700_bbff.txt`. This table covers every callable entry
point and interrupt handler.

| Address | Purpose |
|---|---|
| `$A000` | Cold init. |
| `$A15D` | Restart / new game. |
| `$A197` | Main loop / input sampling. |
| `$A1F5` | Console/joystick handling and skill cycling. |
| `$A271` | Object pass 1: remap, patrol, X wrap. |
| `$A312` | Player control and throttle. |
| `$A3BC` | Level-start transition. |
| `$A3E9` | Collision/score/refuel/bridge. |
| `$A5B7` | Shot launch, object tests, console. |
| `$A6D3` | Row request / feature prep. |
| `$A9D7` | Scroll pointer and seam patch. |
| `$AA7A` | Compose one 48-byte row. |
| `$AB80` | Object aging and spawn. |
| `$AE99` | HUD/score/fuel/collision map. |
| `$AF5B` | Draw remaining-lives icons at `$1033+`. |
| `$AF80` | Level descriptor init. |
| `$AFD0` | Land colour / flash. |
| `$AFEC` | Fill `$AA`. |
| `$AFF0` | Fill `$FF`/`$AA`. |
| `$AFF4` | Fill `$55`/`$AA`. |
| `$B00F` | Overlay copy. |
| `$B01A` | Template copy with advance. |
| `$B034` | 2-byte bank pattern. |
| `$B03B` | 2-byte bank pattern body (shared entry). |
| `$B04B` | Advance destination. |
| `$B05C/B068/B06E/B07C/B064` | HUD writers (score/fuel/skill/lives). |
| `$B0C7/B0D8` | DL seam patch. |
| `$B0F1` | Game init. |
| `$B183` | LFSR step. |
| `$B194` | Level advance. |
| `$B1C0` | Level/explosion transition. |
| `$B221/B223` | Clear shot column. |
| `$B247` | Swap 1P/2P state. |
| `$B261` | High-score compare. |
| `$B298` | High-score checksum. |
| `$B2A5` | Deferred VBI / sound / marquee. |
| `$B485` | IRQ stub. |
| `$B500` | DLI part 1. |
| `$B72A` | DLI part 2. |

## 12. Extracted assets

All under `assets/atari/extracted/` (see its `README.md`):

* `charset.bin/.png` — 128 glyphs used by the game.
* `hud_seed.bin`, `hud_runtime.bin`, `hud_reference.png`.
* `sprites/objects.bin/.json/.png` — the nine real P3 objects (aux 7-15),
  top-down, 2 px/bit, with per-row colour swatches; `objects_all.png` shows
  all 16 entries and marks aux 0-6 as not P3 sprites.
* `sprites/player.bin/.json/.png` — the three flight shapes (7/8/9) and
  `player_all.png` with all 10 table entries.
* `sprites/gauge.json`, `sprites/marquee_colors.bin`.
* `tables.json` — level, score, spawn, sound, river tables, marquee text.
* `river_rows_157.bin`, `river_reference.png`, `river_state.json`.
* `full_frame_reference.png` — a full 336×224 frame rendered only from the
  captured RAM, the extracted ROM shapes and the documented display model.
  It matches `runtime/scroll/screen_final.png` for the playfield, fuel depot,
  enemies and player jet; the HUD band is positioned correctly but its
  glyph/column layout still needs Phase 3 polish.
* `palette.json`.

`src/atari_data.h` embeds the charset, sprite shapes/colours, level/score/
spawn/sound tables, bank patterns and river templates as C arrays; the
generated `src/atari_data.c` emits them once (define `ATARI_DATA_IMPL` before
including the header if you prefer a single file).

## 13. Verification and open items

Confirmed with the emulator:

* PMG page mapping, DLI write address, object Y mapping, player shape
  location, mode-E geometry, ring/DL segments, palette, live river rows.
* The row reference PNG matches the gameplay screenshot's playfield.

Still to verify dynamically (Phase 4 acceptance):

1. Exact bank/fork branch decisions in `$AA7A-$AB7F` (especially island
   split/merge conditions) by comparing generated rows to
   `river_rows_157.bin` for each scrolled frame.
2. Score awards for fuel (candidate 80) and bridge (candidate 500) and
   whether `$BB0B` is indexed by aux or type.
3. Fuel consumption/refuel rates and the 16-bit gauge.
4. Object slot 10 sentinel semantics and spawn timing.
5. The `$BF` PAL/NTSC detection path (fuel gauge Y offset).
6. Marquee glyph mapping (`$B84B-$B8D6`) on the real screen.

Dynamic observations from `assets/atari/runtime/play_left/` (START, hold
left + fire, 180 frames):

* `$57` (player X) moved 124 → 111; `$5E` changed 0 → 7 (bank left).
* `$76` (throttle) moved 128 → 0 → 233, confirming 0 = fastest.
* `$6A` (lives) changed 4 → 3 after the jet hit the left bank.
* `$5D` (level countdown) counted 23 → 14.
* `$52/$53` tracked the bank pixel positions (e.g. 27/72).
* `$29=0` during play, `$82` in attract; `$50=$FF` when no shot is active.
* `assets/atari/runtime/play_fire/` (600 frames, fire held): score went
  0 → 6 → 9 and lives 4 → 3 → 2. The +6 and +3 match `$BB0B` entries for
  aux 8/9/12 and 13, confirming that the table values are direct BCD points,
  not tens.

These do not block the Phase 2 scaffolding or the Phase 3 static screen;
they are the Phase 4 generator parity work.

## 14. Toolchain note

The bundled GBDK lcc (v2.0, 2025-12-28) rejects the documented `-mnes`
spelling; it wants the explicit port/platform pair:

```powershell
./gbdk/bin/lcc -mmos6502:nes -c -Isrc -o build/foo.o src/foo.c
```

`src/atari_data.h` was compile-checked with this form. The link step should
use `-mmos6502:nes -Wl-j` to emit the `.noi` symbol file.
