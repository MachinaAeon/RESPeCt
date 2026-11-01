# Modding RESPeCt

RESPeCt plays the game straight from your PS1 disc image. Mods change it
without touching the disc: files in a `mods` folder next to `RESPeCt.exe`
take the place of the game's while it plays. The PC version's files
(gun_bio's `PSX_DATA`, `Resource` and `nab` folders) work too, so mods made
for the PC version can be used here.

## Quick start

1. **Get the originals.** Make an empty folder called `extract` next to
   `RESPeCt.exe` and start the game once (it takes about a minute). Every
   file a mod can replace is written there, already named the way a mod
   names it.
2. **Make your mod.** Make a folder in `mods` (the game makes `mods` on its
   first start), copy in the files you want to change, at the same path as
   in `extract`, and edit them.
3. **Play.** On start the game says how many files it took from how many
   mods, and how many it couldn't use. `resurv.log` has a line for each
   file (lines starting `mods:`): where it went, or why not.

```
RESPeCt.exe
extract/                               the originals (step 1)
mods/
  my-mod/
    map/MAP00_1.png                    a map screen
    nab/ENEM11.png                     a zombie's skin
    X/BGM_32.STR/05.wav                a piece of music
    stage/STAGE07/0700/0700_A.hmd      a room's model
```

## What a mod can change

| What | File in the mod | From |
| --- | --- | --- |
| Any file of the disc that isn't game code or a stream | its path on the disc: `B/MAPTIM.BIN`, `OPTTIM.BIN`... | `extract` |
| Rooms: models, textures, data | `stage/STAGEnn/RRRR/...` (the PC's names) | `extract`, the PC's `PSX_DATA` |
| Doors, effects | `DOORNEW/...`, `effect/*.HMD` | `extract`, `PSX_DATA` |
| Items, Vincent | `item/.../A.HMD`, `item/.../*.TIM`, `people/VIC700/A.HMD` | `extract`, `PSX_DATA` |
| Bosses' models, enemy textures | `RESOURCE/TAIRANT.HMD`, `RESOURCE/DEBUGTA00.TIM`... | `extract`, the PC's `Resource` |
| 2D screens: maps, results, title and menus, fonts | `map/MAP00_1.png`, `RESULT/RANK_A.png`, `RESOURCE/TITLE10.png`... (BMP works too) | `extract`, `PSX_DATA`, `Resource` |
| An enemy's skin | `nab/ENEMnn.png`, nn its type (00 to 30) | `extract` |
| An enemy's shape, textures and animations, the PC's way | `nab/ENEMnn.NAB`, `nab/ENEMnn.NAC` | the PC's `nab` |
| Music, voices, a movie's sound | `X/BGM_32.STR/05.wav`: a channel of a stream | `extract` |
| A movie's pictures | `M/OPENING.STR/0042.png`: frame 42 | `movie_dump` (below) |
| Any texture in HD | a texture pack (see the README) | |

Paths aren't case-sensitive, and `/` or `\` both work. Pictures can be
PNG, BMP, JPG or TGA.

## The mods folder

- Each folder in `mods` is one mod. Mods apply in alphabetical order, or
  in the order an `order.txt` in `mods` lists them (one folder name per
  line; `#` or `;` starts a comment). When two mods change the same file,
  the later one wins.
- A folder whose name starts with `_` is switched off.
- Files at the top of a mod that aren't the disc's (a readme, pictures of
  it) are left alone.

## Sizes

The game finds its files by their place on the disc, from its own tables,
and loads them into memory it has set aside. RESPeCt moves files and
changes those tables when a file grows, as far as the game's memory goes:

- A whole disc file can use up to the end of its last 2048-byte sector.
- A room's, door's or effect's file can grow: its block is laid out again,
  the archive moved past the end of the disc, and the game's tables
  changed to match. A room's block can take up to 290816 bytes (the
  biggest the game loads), a door's model up to 71680 and its data up to
  69632, an effect set up to 20480.
- An enemy's block can take up to 93 sectors (190464 bytes, packed), the
  biggest the game loads.
- A file inside one of the packed archives (items, enemy textures in
  `Resource`) keeps its size: smaller is fine, bigger isn't.
- Pictures are written at the size the PS1 has for them; another size is
  scaled to it. For more detail than the PS1 has, use a texture pack.

The log says how big each changed block is, and by how much a file that
doesn't fit is too big.

## 2D pictures

The PC version has its 2D pictures as BMP files; 72 of them are pictures
of the PS1 disc at the same size (maps, results, title and menu screens,
fonts, effects). Give one by its PC path, as a BMP, PNG, JPG or TGA, and
RESPeCt writes it into the PS1 picture in that picture's own format:

- 16-bit colour, or a palette of 16 or 256 colours made for the new
  picture; a picture with several palettes (a font's colours) keeps them,
  and the new picture is drawn with the first one's colours;
- see-through where a PNG's alpha is, and where the original was
  see-through and the new picture is still black there.

## Enemies

An enemy's look and movement are in the PC version's `nab` folder as
`ENEMnn.NAB` (the model, with its textures) and `ENEMnn.NAC` (its
animations), nn the enemy's type. They are the PS1's own data read out
into another form, so RESPeCt converts them back:

- from a NAB, its shape: where each vertex is (each body part's, around
  its joint), its normals (the light on it) and where each polygon takes
  its texture from (UVs);
- from a NAB, its textures (edit them with a NAB tool, or give the skin
  as a picture: `nab/ENEMnn.png`, which `extract` has for every type);
- from a NAC, its animations: every key (each bone's rotation and
  position over time) and the animation script;
- from a NAB, its animation tables: where each of a bone's animations
  starts in the script.

**Shapes.** Vertices can be moved as far as you like, and polygons added,
taken away or moved to another body part: the model is then built again
(its skeleton, animation and the rest kept), with new polygons on the
texture page their UVs need. Each enemy in a room has its own copy of the
model, so a model can grow by up to 48 KB (about 1800 more triangles);
the game's memory for a room's enemies ends at 0x801F0000, and if a room
comes near it the log says so.

**Animations.** Keys and commands can be changed, added or taken away
(up to 13107 keys, and 64 KB more than the PS1's): a command takes its
key by the NAC's index, and the NAB's animation tables say where each
animation starts, so a NAC with another number of commands or keys needs
its NAB in the mod too. The enemies' code picks which animation plays, so
new animations aren't used unless they take an existing one's place.

**Types.** 0 to 30 take all of the above. The PC's 34 to 37 are types 16
and 17 on the PS1 and go there. 31 to 33, the bosses loaded in one piece,
take all of it too, with these differences:

- their skin (the polygons shared between bones, which keep the body in
  one piece as it moves) is built again when polygons are added or taken
  away, each bone's part of it with its own vertices and normals; each
  bone's own polygons too. New skin polygons can't go on a material that
  is a sheet of small textures (type 33's: which one they'd use isn't
  known); moving its vertices is fine;
- their animations can grow, laid out again after the model (a boss can
  grow by 160 KB in all);
- their textures are 8-bit pictures inside their own model (type 33's are
  17 small ones the PC stretched to powers of two): each changed picture
  gets a palette made for it (type 33's share one, made for all of
  them), and the copies of them elsewhere on the disc change too.

## Music, voices and movies' sound

The disc's streamed sound (XA: the music in `X/BGM_32.STR`, the voices in
`X/VC/*.STR`, the movies' sound in `M/*.STR`) is in channels, and a mod
replaces one channel at a time with a WAV file (16-bit PCM, mono or
stereo, any rate): `X/BGM_32.STR/00.wav` is channel 0 of `X/BGM_32.STR`.

The game still plays the stream from the disc, so it starts, loops and
stops where it always did; RESPeCt plays the WAV's samples in place of
the channel's, at the same point. A WAV shorter than the channel ends in
silence, a longer one is cut (both in the log). `extract` has every
channel with the right name and length.

## Movies

A movie's pictures are replaced one frame at a time, by the frame's
number in the stream (from 1): `M/OPENING.STR/0001.png`,
`M/OPENING.STR/0002.png`... Each picture is the whole screen while that
frame shows (black bars included), any size up to 4096x4096, stretched to
fill it: keep the original's shape (4:3) and draw it as large as you like,
say 1280x960. RESPeCt follows which of the stream's frames the game is
showing, so the pictures keep the game's own timing, in step with the
sound; a frame with no picture shows the disc's.

To get the original frames, make a folder `movie_dump` next to
`RESPeCt.exe` and play the movies: every frame shown is written there as
it looks on the PS1, with the names a mod needs. Upscale or redraw them,
put them in a mod, and delete `movie_dump` when done (while it's there,
every movie played is written out again).

## Saves

Memory card saves work with or without mods. A quick save (savestate)
made before you added a mod keeps the game's memory as it was; on loading
it, RESPeCt puts the mods' changes to the game's tables back in (the log
says how many). Moving a mod out after making a quick save with it can
leave that save reading the wrong places: load a memory card save then.

## What can't be changed

- **Game code** (`KERNEL.BIN`, `TEST.BIN`, the overlays): RESPeCt runs it
  translated, not from the disc.
- **Texts.** The PC version's messages are pictures of text (BMPs); the PS1
  builds them from font sheets and message data, which are disc files
  (they can be replaced whole, but there's no tool for the text yet).
- **Sound effects**: they are in the PS1's sound banks; the PC's `wav`
  folder isn't used.
- **The PC's movies and animations** (`.avi`, `.mpg`, `.FLC`): give frames
  instead (above).
- Everything else the PC version has in its own forms; the log says so
  for each file.

## Tools

For developers, to make the tables RESPeCt uses from a PC version and the
disc image:

- `tools/mod_pcnames.py` (`port/src/modfs_pc.h`): where each loose PC
  file is on the disc;
- `tools/mod_packnames.py` (`port/src/modfs_pack.h`): PC files inside the
  packed archives;
- `tools/mod_bmpnames.py` (`port/src/modfs_bmp.h`): which BMP is which of
  the disc's pictures (needs numpy);
- `tools/mod_bossnames.py` (`port/src/modfs_boss.h`): where the
  bosses' textures are on the disc (needs numpy and the PC's `nab`);
- `tools/xa_dump.py`: every sound channel as WAV (as `extract` does).

The headless runner (`port/build/.../resurv_headless`) takes `RT_MODS=dir`
for a mods folder, `RT_EXTRACT=dir` and `RT_MOVIE_DUMP=dir` for the
originals, and `RT_DUMP_FILE=lba:size=path` to write a disc file as the
game reads it, mods applied (lba and size from `disc_ids.h`).
