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
  tables. A whole disc file can use up to the end of its last 2048-byte
  sector. A PC version file can use its original size (what's left of its
  place is filled with zeros), and the rooms' files (`stage/...`, packed in
  `B/STGBGn.BIN`) can be bigger: RESPeCt lays the room's block out again,
  moves the archive past the end of the disc and changes the game's tables
  to match (`TEST.BIN`'s block table, `KERNEL.BIN`'s list of where the
  stage files are). A room's block can take up to 290816 bytes, the
  biggest the game itself loads into the memory it has for it
  (0x80108000-0x80150000); the log says how big each changed room is. Other files that grow are refused, with how much
  too big they are in the log.
- **Code.** Files that hold game code (`KERNEL.BIN`, `TEST.BIN`, the
  overlays) can't be replaced: RESPeCt runs that code translated, not from
  the disc.
- **Streams.** Movies and streamed sound (`.STR`) aren't replaced whole:
  their pictures go frame by frame and their sound channel by channel
  (below), and the game still plays the disc's stream for its timing.
- **The PC version's other files.** Enemies (`ENEMnn.NAB/NAC`), items and
  characters (`item`, `people`), 2D pictures (`.BMP`), sound (`.WAV`) and
  movies (`.AVI`) are in other formats than the PS1 disc's, so they aren't
  used. The log says so for each one.

## Music, voices and movies' sound

The disc's streamed sound (XA: the music in `X/BGM_32.STR`, the voices in
`X/VC/*.STR`, the movies' sound in `M/*.STR`) is in channels, and a mod
replaces one channel at a time with a WAV file (16-bit PCM, mono or
stereo, any rate):

```
mods/my-music/X/BGM_32.STR/00.wav      channel 0 of X/BGM_32.STR
```

The game still plays the stream from the disc, so it starts, loops and
stops where it always did; RESPeCt plays the WAV's samples in place of
the channel's, at the same point. A WAV shorter than the channel ends in
silence, a longer one is cut (both in the log).

`tools/xa_dump.py <disc.cue> <folder>` writes every channel of every
stream as a WAV with the right name and length, ready to edit and put
back.

## Movies

A movie's pictures are replaced one frame at a time, with pictures (PNG,
JPG, BMP or TGA, any size up to 4096x4096) named after the frame's
number in the stream, from 1:

```
mods/my-movies/M/OPENING.STR/0001.png     frame 1 of M/OPENING.STR
mods/my-movies/M/OPENING.STR/0002.png     frame 2
```

Each picture is the whole screen while that frame shows (black bars
included), and is stretched to fill it: keep the original's shape (4:3)
and draw it as large as you like, say 1280x960. RESPeCt follows which of
the stream's frames the game is showing, so the pictures keep the game's
own timing, in step with the sound; a frame with no picture in the mod
shows the disc's.

To get the original frames to work from, make a folder `movie_dump` next
to `RESPeCt.exe` and play the movies: every frame shown is written there
as it looks on the PS1 (`movie_dump/M/OPENING.STR/0001.png`...), with the
names a mod needs. Upscale or redraw them, put them in a mod, and delete
`movie_dump` when done (while it's there, every movie played is written
out again).

## Textures

HD replacements for any texture, without changing the disc's files, are
texture packs in the Beetle PSX HW format (see the README): an image named
after the texture's hash in a `textures` folder.

## Tools

- `tools/mod_pcnames.py` builds `port/src/modfs_pc.h` from a PC version's
  `PSX_DATA` folder and the disc image.
- The headless runner (`port/build/.../resurv_headless`) takes `RT_MODS=dir`
  for a mods folder, `RT_MOVIE_DUMP=dir` to write the movies' frames (as
  `movie_dump` does), and `RT_DUMP_FILE=lba:size=path` writes a disc file
  as the game reads it, mods applied (lba and size from `disc_ids.h`).
