# RESPeCt

**RESPeCt** is a free, unofficial PC version of *Resident Evil Survivor*
(PlayStation, USA, `SLUS-01087`). It adds full mouse-and-keyboard aiming,
widescreen and high resolutions, and quick saves.

It contains **no code or data from the game**. You supply your own copy of
the disc, and RESPeCt installs what it needs from it.

## Playing

1. Download `RESPeCt.zip` from [Releases](../../releases) and unzip it anywhere.
2. Put your disc image of *Resident Evil Survivor* (USA) in the same folder
   as `RESPeCt.exe`. Any of these work:
   - a `.bin` and `.cue` pair
   - a `.chd`
   - the `.zip` they came in
3. Run `RESPeCt.exe`. On first launch it checks the disc image and installs
   the game's data into a `data` folder, about 430 MB. After that you can
   delete your disc image.

The image must be an exact copy of the USA release. The European and
Japanese releases can't be used. `.iso` files can't be used either, because
they leave out the music and voices. If something's wrong, RESPeCt tells you
what it found.

Everything RESPeCt writes stays in its own folder: settings, saves, the
memory card and the installed data. To uninstall, delete the folder (back up
`saves\` and `memcard1.mcd` first if you want to keep your progress).

Windows may warn that the program is from an unknown publisher, because it
isn't code-signed. Click **More info → Run anyway**.

### Controls

| | |
|---|---|
| Mouse | aim; left button fires. In menus it moves the cursor (left = OK, right = cancel) |
| F1 | settings: video, sound, keys, cheats |
| F5 / F9 | quick save / quick load (F6 / F7 pick the slot) |
| F11 or Alt+Enter | fullscreen |
| Esc | release the mouse |

The game's own saves (typewriters and payphones) go on a memory card file
next to the exe.

## How it works

The game's code is being decompiled into readable C (`port/decomp/`, over
1,100 functions so far). The C runs natively. The rest of the game's code is
read from your disc and run by a built-in interpreter (`port/src/interp.c`).
The interpreter follows exactly the same rules as the project's static
recompiler, which the developers use. Both builds are checked against each
other RAM-for-RAM across dozens of recorded play sessions
(`tools/eq/`).

The PlayStation hardware is re-created in `port/src/`: the GPU (with a
high-resolution renderer), the geometry coprocessor, sound, the CD drive,
movies and memory cards. Developer notes are in
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md); how to decompile a function is
in [port/decomp/README.md](port/decomp/README.md).

## Building

You need clang and a MinGW-w64 sysroot (for Windows). No game files are
needed to build the release:

```
make -C port windist     # port/build/windist/RESPeCt.exe
make -C port release     # port/build/release/RESPeCt.zip
make -C port dist        # Linux headless runner, for testing
```

`WIN_SYSROOT` points at the sysroot (default `~/toolchain/sysroot`).

## Legal

RESPeCt is an unofficial fan project. It is not affiliated with, endorsed or
sponsored by Capcom. *Resident Evil* and *Resident Evil Survivor* are
trademarks of Capcom Co., Ltd. This repository and the release contain no
game code, graphics, sound or other data from the game. You need your own
legally obtained copy.

## License

RESPeCt is free software, released under the
[GNU General Public License v3.0](LICENSE). You may use, change and share it.
If you distribute a modified version, it must also be under the GPL with its
source code available.

Third-party libraries (in `port/third_party/`, used to read CHD and ZIP
files) keep their own licenses; see
[port/release/THIRD_PARTY_LICENSES.txt](port/release/THIRD_PARTY_LICENSES.txt).
