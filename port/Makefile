# PC port build (Linux, headless runner). Generated code: tools/recomp/recomp.py
CC ?= clang
GEN_CFLAGS = -O1 -w -Iinclude -fno-strict-aliasing
SRC_CFLAGS = -O2 -g -Wall -Wextra -Wno-unused-parameter -Iinclude -Isrc -Ithird_party -Ithird_party/libchdr/include -fno-strict-aliasing
# libchdr (CHD disc images; BSD) with its zlib (miniz, also used for .zip), LZMA, zstd and FLAC
CHDR = third_party/libchdr
CHDR_CFLAGS = -O2 -w -I$(CHDR)/include -I$(CHDR) -Ithird_party -I$(CHDR)/deps/lzma-26.02/include -I$(CHDR)/deps/zstd-1.5.7 -I$(CHDR)/deps/miniz-3.1.2 -DMINIZ_ARCHIVE_APIS -DMINIZ_STDIO
BUILD = build/linux

GEN = $(wildcard gen/*.c)
SRC = $(filter-out src/main_%.c,$(wildcard src/*.c))
DECOMP = $(wildcard decomp/*.c)
GEN_O = $(GEN:gen/%.c=$(BUILD)/gen/%.o)
SRC_O = $(SRC:src/%.c=$(BUILD)/src/%.o)
DECOMP_O = $(DECOMP:decomp/%.c=$(BUILD)/decomp/%.o)

all: $(BUILD)/resurv_headless

$(BUILD)/gen/%.o: gen/%.c include/recomp.h
	@mkdir -p $(dir $@)
	$(CC) $(GEN_CFLAGS) -c $< -o $@

$(BUILD)/src/%.o: src/%.c src/rt.h src/render.h src/texpack.h src/config.h src/disc_ids.h include/recomp.h
	@mkdir -p $(dir $@)
	$(CC) $(SRC_CFLAGS) -c $< -o $@

$(BUILD)/decomp/%.o: decomp/%.c $(wildcard decomp/*.h) include/recomp.h
	@mkdir -p $(dir $@)
	$(CC) $(SRC_CFLAGS) -Idecomp -c $< -o $@

$(BUILD)/chdr.o: $(wildcard $(CHDR)/src/*.c) third_party/chdr_all.c
	@mkdir -p $(dir $@)
	$(CC) $(CHDR_CFLAGS) -c third_party/chdr_all.c -o $@

$(BUILD)/resurv_headless: $(GEN_O) $(SRC_O) $(DECOMP_O) $(BUILD)/src/main_headless.o $(BUILD)/chdr.o
	$(CC) -o $@ $^ -lz -lm -lpthread

clean:
	rm -rf build
.PHONY: all clean

# Windows build (cross-compiled): make win
WIN_SYSROOT ?= $(HOME)/toolchain/sysroot
WIN_CC = clang --target=x86_64-w64-mingw32 --sysroot=$(WIN_SYSROOT)
WBUILD = build/win
WGEN_O = $(GEN:gen/%.c=$(WBUILD)/gen/%.o)
WSRC_O = $(SRC:src/%.c=$(WBUILD)/src/%.o)
WDECOMP_O = $(DECOMP:decomp/%.c=$(WBUILD)/decomp/%.o)

win: $(WBUILD)/ResidentEvilSurvivor.exe

$(WBUILD)/gen/%.o: gen/%.c include/recomp.h
	@mkdir -p $(dir $@)
	$(WIN_CC) $(GEN_CFLAGS) -c $< -o $@

$(WBUILD)/src/%.o: src/%.c src/rt.h src/render.h src/texpack.h src/config.h src/disc_ids.h include/recomp.h
	@mkdir -p $(dir $@)
	$(WIN_CC) $(SRC_CFLAGS) -I$(CHDR)/deps/miniz-3.1.2 -c $< -o $@

$(WBUILD)/decomp/%.o: decomp/%.c $(wildcard decomp/*.h) include/recomp.h
	@mkdir -p $(dir $@)
	$(WIN_CC) $(SRC_CFLAGS) -Idecomp -c $< -o $@

$(WBUILD)/chdr.o: $(wildcard $(CHDR)/src/*.c) third_party/chdr_all.c
	@mkdir -p $(dir $@)
	$(WIN_CC) $(CHDR_CFLAGS) -c third_party/chdr_all.c -o $@

$(WBUILD)/ResidentEvilSurvivor.exe: $(WGEN_O) $(WSRC_O) $(WDECOMP_O) $(WBUILD)/src/main_win32.o $(WBUILD)/chdr.o
	$(WIN_CC) -fuse-ld=lld -mwindows -static-libgcc -Wl,--stack,8388608 -o $@ $^ -lgdi32 -luser32 -lwinmm -lcomdlg32 -lshell32
.PHONY: win

# Release build: no translated game code. The game's code is read from the
# player's disc and run by src/interp.c, with the tables in meta/meta.c
# (tools/recomp/meta.py). make dist (Linux headless), make windist (Windows).
META = $(wildcard meta/*.c)
DBUILD = build/dist
DSRC_O = $(SRC:src/%.c=$(DBUILD)/src/%.o)
DDECOMP_O = $(DECOMP:decomp/%.c=$(DBUILD)/decomp/%.o)
DMETA_O = $(META:meta/%.c=$(DBUILD)/meta/%.o)

dist: $(DBUILD)/resurv_headless

$(DBUILD)/src/%.o: src/%.c src/rt.h src/render.h src/texpack.h src/config.h src/disc_ids.h include/recomp.h
	@mkdir -p $(dir $@)
	$(CC) $(SRC_CFLAGS) -DRT_DIST -c $< -o $@

$(DBUILD)/decomp/%.o: decomp/%.c $(wildcard decomp/*.h) include/recomp.h
	@mkdir -p $(dir $@)
	$(CC) $(SRC_CFLAGS) -DRT_DIST -Idecomp -c $< -o $@

$(DBUILD)/meta/%.o: meta/%.c include/recomp.h
	@mkdir -p $(dir $@)
	$(CC) $(GEN_CFLAGS) -c $< -o $@

$(DBUILD)/chdr.o: $(wildcard $(CHDR)/src/*.c) third_party/chdr_all.c
	@mkdir -p $(dir $@)
	$(CC) $(CHDR_CFLAGS) -c third_party/chdr_all.c -o $@

$(DBUILD)/resurv_headless: $(DSRC_O) $(DDECOMP_O) $(DMETA_O) $(DBUILD)/src/main_headless.o $(DBUILD)/chdr.o
	$(CC) -o $@ $^ -lz -lm -lpthread

WDBUILD = build/windist
WDSRC_O = $(SRC:src/%.c=$(WDBUILD)/src/%.o)
WDDECOMP_O = $(DECOMP:decomp/%.c=$(WDBUILD)/decomp/%.o)
WDMETA_O = $(META:meta/%.c=$(WDBUILD)/meta/%.o)

windist: $(WDBUILD)/RESPeCt.exe

$(WDBUILD)/src/%.o: src/%.c src/rt.h src/render.h src/texpack.h src/config.h src/disc_ids.h include/recomp.h
	@mkdir -p $(dir $@)
	$(WIN_CC) $(SRC_CFLAGS) -I$(CHDR)/deps/miniz-3.1.2 -DRT_DIST -c $< -o $@

$(WDBUILD)/decomp/%.o: decomp/%.c $(wildcard decomp/*.h) include/recomp.h
	@mkdir -p $(dir $@)
	$(WIN_CC) $(SRC_CFLAGS) -DRT_DIST -Idecomp -c $< -o $@

$(WDBUILD)/meta/%.o: meta/%.c include/recomp.h
	@mkdir -p $(dir $@)
	$(WIN_CC) $(GEN_CFLAGS) -c $< -o $@

$(WDBUILD)/chdr.o: $(wildcard $(CHDR)/src/*.c) third_party/chdr_all.c
	@mkdir -p $(dir $@)
	$(WIN_CC) $(CHDR_CFLAGS) -c third_party/chdr_all.c -o $@

WINDRES ?= $(firstword $(wildcard /usr/lib/llvm-*/bin/llvm-windres) llvm-windres)
$(WDBUILD)/respect_res.o: res/respect.rc res/respect.ico
	@mkdir -p $(dir $@)
	$(WINDRES) --target=x86_64-w64-mingw32 -I res -i res/respect.rc -o $@

$(WDBUILD)/RESPeCt.exe: $(WDSRC_O) $(WDDECOMP_O) $(WDMETA_O) $(WDBUILD)/src/main_win32.o $(WDBUILD)/chdr.o $(WDBUILD)/respect_res.o
	$(WIN_CC) -fuse-ld=lld -s -mwindows -static-libgcc -Wl,--stack,8388608 -o $@ $^ -lgdi32 -luser32 -lwinmm -lcomdlg32 -lshell32
.PHONY: dist windist

# The download: the exe, the player's readme and the licenses, in a zip.
release: $(WDBUILD)/RESPeCt.exe release/README.txt release/THIRD_PARTY_LICENSES.txt ../LICENSE
	rm -rf build/release && mkdir -p build/release/RESPeCt
	cp $(WDBUILD)/RESPeCt.exe release/README.txt release/THIRD_PARTY_LICENSES.txt build/release/RESPeCt/
	cp ../LICENSE build/release/RESPeCt/LICENSE.txt
	cd build/release && python3 -c "import shutil; shutil.make_archive('RESPeCt', 'zip', '.', 'RESPeCt')"
	@if [ -e ../disc/image/survivor.bin ]; then python3 ../tools/audit_exe.py $(WDBUILD)/RESPeCt.exe | tail -1; fi
.PHONY: release
