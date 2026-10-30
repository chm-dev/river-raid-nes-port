# River Raid NES port - alternative GNU make build.
#
# build.ps1 is the primary Windows build (it also validates the header and
# RAM layout).  This Makefile mirrors the compile/link commands for CI or
# MSYS/MinGW environments.

GBDK   ?= ./gbdk
LCC    := $(GBDK)/bin/lcc
SRC    := $(wildcard src/*.c)
OBJ    := $(patsubst src/%.c,build/obj/%.o,$(SRC))
ROM    := roms/river_raid.nes
NOI    := roms/river_raid.noi

CFLAGS  := -mmos6502:nes -Isrc -Wf--opt-code-size
LDFLAGS := -mmos6502:nes -Isrc -Wm-yoA -Wl-j -Wl-m -Wf--opt-code-size

.PHONY: all clean test

all: $(ROM)

# Modules placed in switchable ROM bank 1 (same list as $bankedSources in build.ps1).
BANKED := entities entity_gfx player hud
BANKFLAGS = $(if $(filter $(basename $(notdir $<)),$(BANKED)),-Wf-bo1)

build/obj/%.o: src/%.c
	@mkdir -p build/obj
	$(LCC) $(CFLAGS) $(BANKFLAGS) -c -o $@ $<

$(ROM): $(OBJ)
	@mkdir -p roms
	$(LCC) $(LDFLAGS) -o $@ $(OBJ)
	@rm -f roms/river_raid.ihx roms/river_raid.rom
	python tools/patch_nes_header.py $(ROM)

clean:
	rm -rf build
	rm -f $(ROM) $(NOI) roms/river_raid.map roms/river_raid.ihx roms/river_raid.rom

test: $(ROM)
	node tests/nes_smoke.mjs
	node tests/nes_scroll_hud.mjs
