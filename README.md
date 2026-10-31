# RESPeCt

**RESPeCt** is a free, unofficial PC version of *Resident Evil Survivor*
(PlayStation, USA, `SLUS-01087`). It adds full mouse-and-keyboard aiming,
widescreen and high resolutions, controller support, HD texture pack
support, and quick saves.

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

### Running slowly?

RESPeCt draws the game with your CPU unless you turn on **F1 › Video ›
Graphics card**: then the graphics card draws it (Direct3D 11), much
faster, and resolutions up to 8x become available. Drawn larger than your
screen, the picture is shrunk to fit and edges come out smooth. HD
texture packs work on both the graphics card and CPU. With the CPU the
resolution setting matters most: the first start picks a resolution
to suit your CPU, and if the game still can't keep up it lowers it by
itself (with a message). You can change it any time in **F1 › Video ›
Resolution**.

### HD textures (optional)

RESPeCt can use HD texture packs made for the Beetle PSX HW emulator. We
recommend the **[RE Survivor HQ Texture Pack by Leigiboy](https://www.moddb.com/mods/re-survivor-hq-texture-pack/downloads/re-survivor-hq-textures)**
on ModDB. All credit for the pack goes to Leigiboy.

> RESPeCt is not affiliated with Leigiboy or the texture pack in any way.
> If HD textures look wrong in RESPeCt, please
> [open an issue](../../issues) here. It's most likely something RESPeCt
> needs to fix, so please don't contact Leigiboy about it.

1. Download the pack and put the `.rar` in the same folder as `RESPeCt.exe`
   (a `.zip` or `.7z` works too).
2. Start RESPeCt. The first time, it unpacks the pack into a `textures`
   folder (about a minute) and offers to move the archive to the Recycle Bin.

Windows 11 can unpack `.rar` and `.7z` by itself. On Windows 10, unpack the
archive into the RESPeCt folder with 7-Zip or WinRAR first. An unpacked pack
folder works anywhere in the RESPeCt folder, and `texture_pack =` in
`resurv.ini` can point to one somewhere else (such as RetroArch's copy).

**F2** turns HD textures on and off. They show at resolution 2x and higher.

### Controls

| | |
|---|---|
| Mouse | aim; left button fires. In menus it moves the cursor (left = OK, right = cancel) |
| F1 | settings: video (resolution, widescreen, brightness...), sound, keys, controller, cheats |
| F2 | HD textures on/off (with a texture pack) |
| F5 / F9 | quick save / quick load (F6 / F7 pick the slot) |
| F11 or Alt+Enter | fullscreen |
| Esc | release the mouse |

**Controller:** Xbox controllers work out of the box (PlayStation and others
through Steam Input). Left stick moves, right stick aims, RT fires, LT quick
search, A OK/run, B cancel, X reload, LB/RB change weapon, L3 sprint. Hold A
to fast-forward cutscenes. L3 + R3 opens the settings. Buttons can be changed
in `resurv.ini`.

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

## Credits

- HD texture pack: [RE Survivor HQ Texture Pack](https://www.moddb.com/mods/re-survivor-hq-texture-pack/downloads/re-survivor-hq-textures)
  by **Leigiboy** (separate download; not affiliated with RESPeCt).

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
