# Orca Seaflake for Teletype: the Mac app (emulator + flasher + help)
#
#   make            build the C core (libseaflake-emu.a) and the Swift app binary
#   make app        build "Orca Seaflake TT.app" (hex, instructions and operator tables inside)
#   make install    copy the app to /Applications
#   make test       run the engine tests (emu/test_seaflake.c)
#   make shot       the SDL render tool (seaflake-shot --shot out.bmp patch.orca ms [view]), needs sdl2
#
# The app is SwiftUI (App.swift, Flash.swift) over the firmware's own C code:
# tt_emu.c replaces module/main.c + flash.c, everything else is the module's.

ROOT = ..
CC ?= cc
CFLAGS = -std=gnu99 -O2 -g -Wall -Wno-unused-function -Wno-unused-variable \
	-Wno-unused-but-set-variable -Wno-sign-compare -Wno-missing-braces \
	-Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fno-common \
	-Ishim -I$(ROOT)/src -I$(ROOT)/module -I$(ROOT)/libavr32/src

TT_SRC = $(filter-out $(ROOT)/src/match_token.c,$(wildcard $(ROOT)/src/*.c)) \
	$(wildcard $(ROOT)/src/ops/*.c) $(wildcard $(ROOT)/src/seaflake/*.c) \
	$(addprefix $(ROOT)/module/,edit_mode.c grid.c seaflake_help.c line_editor.c \
		live_mode.c pattern_mode.c preset_r_mode.c preset_w_mode.c seaflake_mode.c sf_ui.c dream.c) \
	$(addprefix $(ROOT)/libavr32/src/,font.c kbd.c music.c random.c region.c \
		timers.c util.c euclidean/data.c euclidean/euclidean.c)
CORE_SRC = $(sort $(TT_SRC) $(ROOT)/src/scanner.c) tt_emu.c
HDRS = $(wildcard *.h shim/*.h $(ROOT)/src/seaflake/*.h $(ROOT)/module/*.h)
DOCS = $(ROOT)/docs/manual

APP = Orca Seaflake TT.app
SWIFT = swiftc -O -parse-as-library -target arm64-apple-macos13.0

all: seaflake-tt

$(ROOT)/src/scanner.c: $(ROOT)/src/scanner.rl
	ragel -C -G2 $< -o $@

# the C core, one object per source, into build/
build/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

CORE_OBJ = $(patsubst %.c,build/%.o,$(subst ../,root/,$(CORE_SRC)))
build/root/%.o: $(ROOT)/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

libseaflake-emu.a: $(ROOT)/src/scanner.c $(CORE_OBJ)
	rm -f $@
	ar rcs $@ $(CORE_OBJ)

seaflake-tt: libseaflake-emu.a App.swift Flash.swift bridge.h tt_emu.h
	$(SWIFT) -import-objc-header bridge.h -I. -I$(ROOT)/src -o $@ App.swift Flash.swift \
		libseaflake-emu.a -framework CoreMIDI -framework CoreFoundation -framework IOKit

test_seaflake: test_seaflake.c $(CORE_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE_SRC) test_seaflake.c

test: test_seaflake
	./test_seaflake

# the old SDL front end, kept for --shot renders of the OLED (docs)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || echo -I/opt/homebrew/include/SDL2 -D_THREAD_SAFE)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null || echo -L/opt/homebrew/lib -lSDL2)
shot: seaflake-shot
seaflake-shot: $(CORE_SRC) main.c $(HDRS)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(CORE_SRC) main.c $(SDL_LIBS) -framework CoreMIDI -framework CoreFoundation

app: seaflake-tt
	rm -rf "$(APP)"
	mkdir -p "$(APP)/Contents/MacOS" "$(APP)/Contents/Resources"
	cp seaflake-tt "$(APP)/Contents/MacOS/Orca Seaflake TT"
	cp Info.plist "$(APP)/Contents/Info.plist"
	cp icon.icns "$(APP)/Contents/Resources/icon.icns"
	cp $(ROOT)/dist/orca-seaflake-teletype.hex "$(APP)/Contents/Resources/"
	cp $(ROOT)/dist/orca-seaflake-teletype.hex.sha256 "$(APP)/Contents/Resources/"
	cp $(DOCS)/flashing.txt $(DOCS)/ops.tsv $(DOCS)/keys.tsv "$(APP)/Contents/Resources/"
	codesign --force --deep -s - "$(APP)"

install: app
	rm -rf "/Applications/$(APP)"
	cp -R "$(APP)" /Applications/

clean:
	rm -rf build libseaflake-emu.a seaflake-tt seaflake-emu seaflake-shot test_seaflake "$(APP)" *.dSYM

.PHONY: all app install clean test shot
