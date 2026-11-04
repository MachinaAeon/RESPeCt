# Modding RESPeCt

RESPeCt plays the game straight from your PS1 disc image. Mods change it
without touching the disc: files in a `mods` folder next to `RESPeCt.exe`
take the place of the game's while it plays. The PC version's files
(gun_bio's `PSX_DATA`, `Resource` and `nab` folders) work for the mappings
listed below. This is not blanket compatibility with every PC mod: PC
message and diary BMPs have different representations and pagination.

## PC asset packages

For a PC mod that retains `PSX_DATA`, `Resource` and `nab`, see
[Automatic PC asset-mod compatibility](PC_RESOURCE_COMPATIBILITY.md).
The candidate accepts the PSX_DATA wrapper. `tools/pc_mod.py check` accounts
for every supplied file; `import` refuses unknown resources and conflicts
instead of copying a silently partial mod. A recognized route still needs
runtime and gameplay verification. Full PC asset compatibility remains open.

The candidate also accepts the verified PC `noise/noise.bin` layout through
an ordered delta adapter, and the event-verified `WAV/ENEMY/DOG/SND-001.WAV`.
See [PC audio compatibility](PC_AUDIO_COMPATIBILITY.md); this does not mean
that all PC WAV filenames or enlarged banks are supported.

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
    wav/Scene05_1.wav                  a voice (the PC's name)
    NOISE/001/02.wav                   a sound effect
    NOISE/236/music1.wav               a room's music, recorded
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
| Room messages, item names/descriptions, three inventory labels and character mappings | `text/ROOMS.txt`, `GAME.txt`, `ITEMS.txt`, `UI.txt`, `LIST.txt`, `CHARS.txt` | `extract` |
| A PS1 diary page | `diary/00/01.png` (document 0, page 1) | `extract` |
| An enemy's skin | `nab/ENEMnn.png`, nn its type (00 to 30) | `extract` |
| An enemy's shape, textures and animations, the PC's way | `nab/ENEMnn.NAB`, `nab/ENEMnn.NAC` | the PC's `nab` |
| Streamed music, voices, a movie's sound | `X/BGM_32.STR/05.wav`: a channel of a stream; or the PC's name, `wav/Scene05_1.wav` | `extract`, the PC's `PSX_DATA/wav` |
| Sound effects (and the instruments of the rooms' music) | `NOISE/012/03.wav`: sound 3 of the bank in block 12 | `extract` |
| A room's music | `NOISE/236/music1.seq` (the PS1's sequence), or `NOISE/236/music1.wav` (a recording, as the PC has its music) | `extract` |
| A movie's pictures | `M/OPENING.STR/0042.png`: frame 42 | `movie_dump` (below) |
| Any texture in HD | a texture pack (see the README) | |
| Weapons' fire rate and damage, damage per enemy type, the small enemies' hit points | `data/COMBAT.txt` | `extract` |
| A room's enemies (type, place, hit points) and pickups | `data/ROOMS.txt` (`@block`, as in `text/ROOMS.txt`) | `extract` |
| The game's code: hooks on its functions, its memory | `plugins/name.dll` (native code, off unless switched on) | your own (below) |

Paths aren't case-sensitive, and `/` or `\` both work. Pictures can be
PNG, BMP, JPG or TGA.

## The mods folder

- Each folder in `mods` is one mod. Mods apply in alphabetical order, or
  in the order an `order.txt` in `mods` lists them (one folder name per
  line; `#` or `;` starts a comment). When two mods change the same file,
  the later one wins.
- A folder whose name starts with `_` is switched off.
- The settings (F1) have a **Mods** page: each mod on or off (kept in
  `mods/disabled.txt`, one folder name a line), plugins and the bigger
  enemy models on or off, a button that opens the mods folder and one that
  asks for the originals (`extract`). Changes apply on the next start.
- A mod made for the PC version goes in as it is, `PSX_DATA` folder and
  all: copy it into `mods`. `tools/pc_mod.py check` (or the portable
  importer) says beforehand which of its files RESPeCt uses.
- Files at the top of a mod that aren't the disc's (a readme, pictures of
  it) are left alone.

## Sizes

The game finds its files by their place on the disc, from its own tables,
and loads them into memory it has set aside. RESPeCt moves files and
changes those tables when a file grows, as far as the game's memory goes:

- A whole disc file can use up to the end of its last 2048-byte sector.
- A room's, door's or effect's file can grow: its block is laid out again,
  the archive moved past the end of the disc, and the game's tables
  changed to match. A room's block can take up to 1 MB (1048576 bytes,
  less if the mods' pictures already fill much of the extra memory); the
  PS1 has room for 290816 (the biggest the game loads). A bigger one moves
  the rooms' memory, see below. The scenery slots of stages 1, 5 and 6 keep
  their own sizes (20, 12 and 23 sectors). A door's model can take up to
  71680 bytes and its data up to 69632, an effect set up to 20480.
- An enemy's block can take up to 93 sectors (190464 bytes, packed), the
  biggest the game loads.
- A room's sound set (its bank and music, `NOISE.BIN`) can grow to 290816
  bytes, so a sequence can be bigger. A sound bank can't: it goes whole into
  the sound chip's 512 KB, shared with the other banks open (see below).
- A file inside one of the packed archives (items, enemy textures in
  `Resource`) keeps its size: smaller is fine, bigger isn't.
- Pictures keep their native TIM dimensions and layout. Larger supported
  pictures also register an HD replacement (below); native data remains
  suitable for the original code. Diary pages must fit their original
  compressed block after conversion; otherwise the original block stays.
- The runtime has 8 MiB of main RAM, of which the original game owns 2 MiB.
  Only explicitly relocated text data uses the extra area here. This does
  not enlarge every model, room, animation or sound buffer automatically.

The log says how big each changed block is, and by how much a file that
doesn't fit is too big.

### The rooms' memory

The game keeps the room it's in at 0x80108000-0x80150000 (288 KB): its
block of `B/STGBGn.BIN` (or stages 1, 5 and 6's scenery slots), and after
it what the room uses for a while: the next room's block, read while a
door opens, the room's sound set, a script's picture or music. When a
mod's room block is bigger than 290816 bytes, RESPeCt moves all of that to
the extra memory, bigger by as much (the log says
`mods: the rooms' memory moved to 80200000-...`). The game's code that
builds those addresses is changed with it; what only borrows the old place
for a moment (enemy files, sound banks, a door's model, the title's and
the typewriter's pictures) stays there, out of the room's way now. Without
such a room nothing moves: the memory is the PS1's, byte for byte.

Rooms, doors, the scenery stages, game loads and quick saves were checked
with a room 415744 bytes big: the same frames as without the mod. A quick
save made with the rooms' memory in the other place (before adding such a
mod, or after removing it) is refused; load a memory card save instead.

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

## Text and fonts

`extract/text` contains UTF-8 files. Each entry starts on its own `@` line;
edit the following lines up to the next entry. Keep the identifiers, and
retain the original control tags unless deliberately changing the prompt.
Only entries supplied by a mod replace the originals; later mods win.

| File | Identifier | Content |
| --- | --- | --- |
| `text/ROOMS.txt` | `@block.number`, e.g. `@0.0` | ROOMENV messages |
| `text/GAME.txt` | numeric `@number` | Item/file names and the TEST strings present in the extraction |
| `text/ITEMS.txt` | numeric `@number` | BIGTIM item descriptions |
| `text/LIST.txt` | numeric `@number`, 0–44 | Optional compact inventory names; full GAME names remain intact |
| `text/UI.txt` | `@RETURN`, `@NO_ITEM`, `@FULLY_LOADED` | Three inline inventory labels, stored in mod RAM |
| `text/CHARS.txt` | `character glyph-number`, one mapping per line | Custom glyph mapping, e.g. `ì 128` |

A literal line break, or `{n}`, starts a new line. Other tags are `{sp}`
(space), `{more}`, `{more2}`, `{stop}`, `{wait 30}`, `{color 2}`,
`{speed 1}`, `{choice1}`, `{choice2}`, `{choiced}`. Advanced round-trip
tags are `{g128}` (glyph), `{xFFFF}` (raw code) and `{noend}`. The end code
is added automatically unless `{noend}` is given. Never remove a choice or
wait tag merely to make a translation fit: it can control an event.

Draw added characters in `FONT/FONT186.png` through `FONT188.png`, then
map them in `CHARS.txt`; cells 128–143 are available in the existing
18-column, 8x14 glyph layout. Text files being UTF-8 does not create a
Unicode font. Custom tokens use the longest match; ties use the later
mod's mapping.

Item names (`GAME` entries 0–44) and the three UI labels must terminate
and fit the original inventory row: at most 27 codes before the end,
including colour codes. Longer item names also produce a layout warning;
this safety limit is not a guarantee that a name fits the visible box.
For a long full name, supply a compact list-only name in `text/LIST.txt`
with the same GAME identifier: at most 18 visible glyphs (144 pixels),
colour tags and the normal end code. New lines, prompts and waits are
rejected in compact names. For example:

```
# text/GAME.txt
@10
Proiettili per pistola
```

```
# text/LIST.txt
@10
Munizioni pistola
```

The list shows the compact name; other consumers retain the full name.
The separator Return/NO ITEM labels belong in UI.txt. Nothing is shortened
automatically. Check your authored names on the actual screen. ROOMENV offsets remain
signed 16-bit offsets; oversized text parts are rejected before writing.
Item descriptions still need to fit their BIGTIM block. These controls do
not provide automatic wrapping or establish complete translation coverage.
Menu labels drawn as pictures must be edited as pictures; other unmapped
strings still need identification. PC `Message`/`eventmsg` BMPs are not
converted into PS1 glyph data automatically. The opt-in
[PC bitmap manifest](PC_BITMAPS.md) can instead map authored raster
messages, pickup images and adapted diary crops to original identities,
while preserving native control streams. It does not infer missing text
or ambiguous page correspondence.

## Item pictures and HD replacements

`BIGTIM/00.png` through the indices provided by `extract` replace the
large item pictures. The already mapped PC picture paths (maps, title,
results and menus) use the same HD registration as BIGTIM.

For these paths and diary pages, an image at least 2x the native width
and height can also be drawn as an HD texture, at a whole scale factor.
This uses the existing **F2 / HD textures** setting and resolution 2x or
higher on either CPU or GPU. Native TIM data is still converted normally;
HD registration does not change game logic or native VRAM dimensions.

Indexed TIMs with several CLUT rows now receive a separate HD image for
each distinct palette. Author the replacement using the first palette:
high-resolution colours are assigned to its indices, then recoloured
through each original palette. An unchanged native index is preserved
where possible, including equal colours that differ in another palette.
Transparency follows the native converter; STP black is not transparent.
This is palette-aware replacement, not unrestricted true-colour drawing
for every original palette.

The FONT186/FONT187/FONT188 sheets share palette VRAM. Their HD variants
also include the other sheets' palette banks, because inventory text can
use FONT187 colours with FONT188 pixels. Keep the extracted atlas layout,
including unused cells; a palette contact sheet is not the atlas. The
native TIM conversion, glyph codes, dimensions and menu logic stay intact.

24-bit TIMs are not promoted. Each promotion is checked against the
configured HD memory budget before allocation; this is not a cumulative
budget guarantee for all mod images. A log line says `with an HD
replacement` only when registration actually succeeded. Loading a state
restores HD tracking only for complete known rectangles that still match
VRAM.

## Diary pages

`diary/00/01.png` means PS1 document 0, page 1 (decimal, starting at 1).
Use the names from `extract/diary`: there are 13 documents and 90 pages
in the supported USA disc. They include the title pages. Pages are TIMs
compressed inside `CONFFILE.BIN` (two Fs). A replacement keeps its TIM
layout, the document's page count and the archive's original block size.
The block is rebuilt and committed only after it fits; failure preserves
the original. Larger images can also supply HD detail as described above.

PC `diary/file00/FILE00_1.BMP` is **not** an alias for PS1 page 1: the
editions differ in page order and how text is split between pages. Adapt
a PC translation to the extracted PS1 pages; do not simply rename it.
Background/cover resources and changing document page counts are outside
this per-page interface.

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
texture page their UVs need. The loader retains shared model data and contiguous copies of instance
work. The current converter permits model growth of up to 48 KB
(about 1800 more triangles);
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

## Experimental dynamic model buffers

[Dynamic model and animation buffers](DYNAMIC_MOD_BUFFERS.md) extend the
room-owned decoded heap when explicitly enabled in `[mods]` with
`dynamic_mod_buffers = 1`. The option defaults to 0. Instance work stays
contiguous; capacity and saved arena metadata are checked. Compressed
staging, SPU and original format limits remain. Larger decoded resources
do not imply every fixed game buffer is relocatable.

## Game data: weapons, enemies, pickups

Two text files hold numbers of the game itself; `extract` writes them
with comments, and given back unchanged they leave the disc as it is.

- `data/COMBAT.txt` (TEST.BIN; the formulas are in `port/decomp/combat.c`):
  `rate <weapon> <n>` each weapon's fire rate (the game uses it doubled; a
  weapon is its item, named in the comment), `shot <kind> <d1> <d2> <d3>
  <class>` each shot kind's base damage and damage class (the kind is the
  weapon; the grenade gun's are 8 to 10 by the grenades loaded),
  `factor <enemy type> <five percents>` how much each damage class hurts
  each of the 34 enemy types, `hp <n> <points>` the hit points of the
  small enemies (types 0 to 10) by their placement's value, and `chance`
  the odds for a value of 15 or more, which picks one at random. A quick
  save made before the mod gets these numbers when it's loaded.
  The bigger enemies and the final boss (ENEMPROG.BIN, each enemy
  program's own table): `enemy <type> <h0> <h1> <h2> <h3>` the four hit
  points of types 11 to 33 (the placement's value 0-3 picks one, 4 and up
  one at random: Cerberus 11, Tyrant 22, Hunter 28, the final boss 32 and
  33...), and `part <type> <shot kind> <ten percents>` the body-part
  factors of every program, zombies (type 0) included: a row per shot
  kind, a column per body part. They apply from the next time the
  program is loaded (the next room with that enemy). Every fight is one
  of these types, bosses included (checked: the Tyrant spawns with the
  numbers given). Types 50 and up aren't enemies with hit points: they're
  the stages' things and people (breakables, actors in scenes, mostly
  placed hidden), only alive or not. The zombies of stage 8 room 0's
  ambush get 200 from that stage's own code.
- `data/ROOMS.txt` (ROOMENV.DAT; `port/decomp/room_loader.c`): for each
  room (`@block`, block = stage x 30 + room) its `enemy` lines (type, the
  game flag that stops it from being placed, position, yaw, pose, the
  animation it starts in, +32768 to start hidden, and its hit points
  value) and `item` lines (item, the flag set once it's taken, position,
  rotation). A room in the file has exactly the lines given: move,
  change, add or take away enemies and pickups. The game keeps room for 7
  objects (enemies and pickups together) in a room loaded at once and 10
  in all, so a room can't have more than 7, or its own number where the
  original has more; the log says so. The room's block grows as needed.

## Plugins: changing the game's code

What a DLL does to the PC version, a plugin does here: a 64-bit Windows
DLL in a mod's `plugins` folder (`mods/my-mod/plugins/name.dll`) that
exports `respect_plugin_init`. Through the interface in
[port/include/respect_plugin.h](../port/include/respect_plugin.h) it

- hooks any function of the game by its PS1 address (the names and
  addresses are in `symbol_addrs.txt` and the decompiled `port/decomp`):
  code before it, which can read and change its arguments or skip it and
  give its result, and code after it, which can change the result;
- reads and writes the game's memory (8 MB: the PS1's 2 MB, then what
  mods use) and its registers, and calls the game's functions;
- runs code every frame.

[port/plugins/example.c](../port/plugins/example.c) logs every room that
loads and every message box's text, and gives back health over time:

```
clang --target=x86_64-w64-mingw32 -shared -O2 -o example.dll example.c -I port/include
```

Plugins are programs, not data: they run only when the player sets
`mod_plugins = 1` in `[mods]` of `resurv.ini` (`RT_PLUGINS=1` for the
headless runner). Otherwise the log says each one wasn't used. With no
plugin hooking anything the game runs exactly as without them. A quick
save keeps the game's memory, not a plugin's own variables: keep
anything that has to survive a save in the game's memory.

## Streamed sound: voices, music, movies' sound

The disc's streamed sound (XA: the music in `X/BGM_32.STR`, the voices in
`X/VC/*.STR`, the movies' sound in `M/*.STR`) is in channels, and a mod
replaces one channel at a time with a WAV file (16-bit PCM, mono or
stereo, any rate): `X/BGM_32.STR/00.wav` is channel 0 of `X/BGM_32.STR`.
The PC version has the same sound as WAVs in `PSX_DATA/wav`, one per
channel (`Scene05_1.wav` is channel 0 of `X/VC/SCENE05.STR`); those names
work too, so the PC's voice mods work as they are.

The game still plays the stream from the disc, so it starts, loops and
stops where it always did; RESPeCt plays the WAV's samples in place of
the channel's, at the same point. A WAV shorter than the channel ends in
silence, a longer one is cut (both in the log). `extract` has every
channel with the right name and length.

## Sound effects

The sound effects, and the instruments the rooms' music plays, are the
sounds of the PS1's sound banks, in `NOISE.BIN`: `NOISE/012/03.wav` is
sound 3 of the bank in block 12. Many banks share sounds (a gunshot is in
every weapon's bank), so a WAV given for one sound changes it in every
bank that has it. `extract` has each sound once, as a WAV at the rate the
game plays it (the pitch is right as you hear it).

The WAV (16-bit PCM, mono or stereo, any rate) is turned into the PS1's
own sound format at that rate. A sound that plays once still plays once; one
that repeats (an engine, a fire) repeats from the WAV's loop start if it
has one (a `smpl` chunk, which `extract` writes and sound editors keep),
else from where the original did.

A bank goes whole into the sound chip's memory (512 KB, shared with the
other banks open at the time), so the bank keeps as much of a longer
sound as it has room for. RESPeCt plays the WAV itself, though: when the
game starts that sound, the sound chip plays the mod's WAV, whole and at
its own quality (16 bits, its own rate), with the game's pitch, envelope
and volume, repeating from the loop start if the original repeats. So a
sound can be as long as you like; only a quick save loaded in the middle
of one goes on with the bank's version.

The PC version's sound effects (`wav/door`, `wav/enemy`...) are other
recordings, not the PS1's sounds: they aren't used.

## A room's music

Each room has a sound set: a bank and one or two sequences, its music
(`NOISE/230` and on: block 230 is set 0). The game starts, stops and fades
them, and times events by where the music is. Two ways to change it:

- `NOISE/236/music1.seq`: a sequence, the PS1's own (`pQES`; `extract`
  has them all; tools that turn MIDI into SEQ and back are common). It
  plays with the room's bank, so its instruments are that bank's sounds,
  which can be changed too (above). It can be bigger than the original.
- `NOISE/236/music1.wav`: a recording, as the PC version has its music.
  The sequence still plays, silent (every note at velocity 0), so
  everything timed by it stays as it was, and RESPeCt plays the WAV while
  it plays: from the start when it starts, paused when it's paused,
  faded when it fades, at the options' music volume. It repeats from the
  WAV's loop start (a `smpl` chunk), else from the beginning.

Which set a room uses: `resurv.log` has a line each time a room's music
starts (`music: NOISE/236/music1.seq`).

The PC version's music WAVs (`PSX_DATA/BGM/012-BGM.WAV`...) are recordings
of these sequences. 18 of them were matched to their sequence by their
sound (each sequence recorded in RESPeCt and compared,
`tools/mod_bgmnames.py`), and go there by their PC name. For the others no
sequence is clearly the same: the log says so; give them by the NOISE name.

## Movies

A movie's pictures are replaced one frame at a time, by the frame's
number in the stream (from 1): `M/OPENING.STR/0001.png`,
`M/OPENING.STR/0002.png`... Each picture is the whole screen while that
frame shows (black bars included), any size up to 4096x4096, stretched to
fill it: keep the original's shape (4:3) and draw it as large as you like,
say 1280x960. RESPeCt follows which of the stream's frames the game is
showing, so the pictures keep the game's own timing, in step with the
sound; a frame with no picture shows the disc's.

A whole movie can also be a video file, read as it plays: the PC
version's own (`avi/Opening.avi`, by its PC name, for the stream of the
same name) or any video as `M/OPENING.STR/video.mp4` (AVI, MPG, MP4, MKV,
WEBM, MOV, WMV: whatever FFmpeg reads, at any size up to 4096x4096). Its
frames are spread over the stream's (the first on the first, the last on
the last), so the timing stays the game's; a frame given as a picture wins
over the video. The stream's sound stays the PS1's (give a WAV per channel
to change it). RESPeCt uses FFmpeg for this: put its 64-bit DLLs (version
6 or 7: avformat, avcodec, avutil, swscale and what they need) next to
RESPeCt.exe. Classic Rebirth's are 32-bit and don't fit. Without them the
log says so and the disc's movie shows.

To get the original frames, make a folder `movie_dump` next to
`RESPeCt.exe` and play the movies: every frame shown is written there as
it looks on the PS1, with the names a mod needs. Upscale or redraw them,
put them in a mod, and delete `movie_dump` when done (while it's there,
every movie played is written out again).

## Saves

Memory card saves work with or without mods. New quick saves include a
content/priority identity for the active source mods. Loading one with
removed or changed mods is refused before the running state is replaced.
Restore the same source files, mod folder names and order, or use a memory
card save. This identity is a compatibility check, not an authenticity
check; unrelated files inside resource subfolders can conservatively
change it too. The identity also includes the actual order of files
inside a mod. If filesystem enumeration changes that order, a quick save
is conservatively refused even when the files are unchanged. Avoid
multiple paths/formats targeting the same resource inside one mod.

A new quick save with no mods can still use the existing conditional RAM
and VRAM reapplication when mods are added. This is not a universal state
migration guarantee. Legacy quick saves have no identity: the log warns
that compatibility is unverified. They cannot reliably detect which mods
were active when saved; keep a backup and prefer a memory card save when
changing mods. Older executables do not enforce the new identity.

## What can't be changed

- **Game code from the disc** (`KERNEL.BIN`, `TEST.BIN`, the overlays):
  RESPeCt runs it translated, not from the disc. Change the code with a
  plugin (above) instead.
- **Automatic mapping of every PC translation BMP.** An explicit
  [bitmap manifest](PC_BITMAPS.md) supports authored mappings; use the PS1 UTF-8 text,
  font and diary interfaces above; not all PC paths have a mapping.
  `item/PIC` contains pickup-message bitmaps, not BIGTIM item pictures.
  See [MODDING_LIMITS.md](MODDING_LIMITS.md) for verified correspondences
  and the information still needed for a complete importer.
- **The PC's sound effects** (`wav/door`, `wav/enemy`...): other
  recordings than the PS1's; give the PS1's sounds instead (above).
- **The PC's `.FLC` animations** other than the ones listed above: give
  frames instead. Its `.avi` and `.mpg` movies play when the FFmpeg DLLs
  are next to the exe (Movies, above).
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
- `tools/mod_wavnames.py` (`port/src/modfs_wav.h`): which stream channel
  each of the PC's `wav` files is (needs numpy);
- `tools/xa_dump.py`: every sound channel as WAV (as `extract` does).

The headless runner (`port/build/.../resurv_headless`) takes `RT_MODS=dir`
for a mods folder, `RT_EXTRACT=dir` and `RT_MOVIE_DUMP=dir` for the
originals, and `RT_DUMP_FILE=lba:size=path` to write a disc file as the
game reads it, mods applied (lba and size from `disc_ids.h`).

## Validation and remaining work

See [MODDING_STATUS.md](MODDING_STATUS.md) for verified capabilities,
remaining limits and the acceptance checks that have not been performed.
Asset-free loader regressions run with `python tools/tests/modfs_test.py`
from the repository root (`CC` can select clang/GCC and target options).


Complete PC diary content can also load automatically from its original
`diary/fileXX/fileXX_N.bmp` paths, without a page manifest. Include every
foreground/background companion for that document, or layer over an installed
complete set. See [PC_DIARIES.md](PC_DIARIES.md) for exact paths, counts, priority,
state compatibility and the separate unsupported PC UI resources.
