# Modding RESPeCt

RESPeCt plays the game straight from your PS1 disc image. Mods change it
without touching the disc: files in a `mods` folder next to `RESPeCt.exe`
take the place of the disc's when the game reads them.

```
RESPeCt.exe
mods/
  order.txt                (optional: the order to apply mods in)
  my-room-retexture/
    stage/STAGE07/0700/0700_A.hmd      a PC version file, by its PSX_DATA path
  my-map-colours/
    B/MAPTIM.BIN                       a disc file, by its path on the disc
```

Each folder in `mods` is one mod. They apply in the order `order.txt` lists
them (one folder name per line; `#` or `;` starts a comment), or else in
alphabetical order. When two mods change the same file, the later one wins.
A `readme.txt` or `readme.md` at the top of a mod is ignored.

On start, the game says how many files it took from how many mods. Every
file and what happened to it (used, or why not) is in `resurv.log`, lines
starting `mods:`.

## Two ways to name a file

**By the disc's path.** Any file on the disc that isn't game code or a
stream: `B/STGBG7.BIN`, `OPTTIM.BIN`, `EV_OBJ.DAT`... (the list is in
`port/src/disc_ids.h`). The whole file is replaced.

**By the PC version's path.** The PC version (gun_bio) keeps the rooms,
doors and effects as loose files in its `PSX_DATA` folder, and they are
byte for byte the files packed in the PS1 disc's archives: room models
(`stage/STAGEnn/RRRR/*.hmd`), their textures and data (`stage/.../*.BIN`,
`*.TIM`), doors (`DOORNEW/...`), effects (`effect/*.HMD`). RESPeCt knows
where each of these 726 files sits on the disc (`port/src/modfs_pc.h`) and
puts a mod's copy in its place, so a mod made for the PC version's files
works here unchanged. Paths are not case-sensitive.

## Limits

- **Size.** The game finds files by their place on the disc, from its own
  tables, so a file can't grow: a whole disc file can use up to the end of
  its last 2048-byte sector, a PC version file up to its original size
  (what's left of its place is filled with zeros). A bigger file is
  refused, with how much too big it is in the log.
- **Code.** Files that hold game code (`KERNEL.BIN`, `TEST.BIN`, the
  overlays) can't be replaced: RESPeCt runs that code translated, not from
  the disc.
- **Streams.** Movies and streamed music and voices (`.STR`) can't be
  replaced yet.
- **The PC version's other files.** Enemies (`ENEMnn.NAB/NAC`), items and
  characters (`item`, `people`), 2D pictures (`.BMP`), sound (`.WAV`) and
  movies (`.AVI`) are in other formats than the PS1 disc's, so they aren't
  used. The log says so for each one.

## Textures

HD replacements for any texture, without changing the disc's files, are
texture packs in the Beetle PSX HW format (see the README): an image named
after the texture's hash in a `textures` folder.

## Tools

- `tools/mod_pcnames.py` builds `port/src/modfs_pc.h` from a PC version's
  `PSX_DATA` folder and the disc image.
- The headless runner (`port/build/.../resurv_headless`) takes `RT_MODS=dir`
  for a mods folder, and `RT_DUMP_FILE=lba:size=path` writes a disc file as
  the game reads it, mods applied (lba and size from `disc_ids.h`).
