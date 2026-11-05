# Modding RESPeCt: a guide for modders

RESPeCt runs the PlayStation version of Resident Evil Survivor natively on
Windows. Everything a mod changes goes in a folder under `mods`, next to
`RESPeCt.exe`: no disc rebuilding, no patching of the game files, nothing
to install. The originals stay as they are; switching a mod off brings the
game back exactly as it was.

If you have made mods for the PC version, most of your files go in as they
are (see *Coming from the PC version*). On top of that RESPeCt lets you
change things the PC version keeps in its code: weapon damage, enemy hit
points, where enemies and pickups are, room sizes, and the game's code
itself through plugins.

The full reference is [MODDING.md](MODDING.md); this guide is the short way in.

## 1. Getting started

1. **Get the originals.** Start RESPeCt, press **F1**, go to the **Mods**
   page and choose **Write the originals**. Start the game again: every file
   a mod can replace is written to the `extract` folder (it takes about a
   minute), already named the way a mod names it: models, textures, pictures
   as PNG, sounds as WAV, texts and game data as plain text files.
2. **Make your mod.** Make a folder in `mods` (for example `mods/my-mod`),
   copy in the files you want to change *at the same path as in `extract`*,
   and edit them.
3. **Play.** At start RESPeCt says how many files it took from how many
   mods and how many it couldn't use. `resurv.log` has a line starting
   `mods:` for every file: where it went, or why it wasn't used and what to
   give instead.

```
RESPeCt.exe
extract/                                 the originals (step 1)
mods/
  my-mod/
    stage/STAGE07/0700/0700_A.hmd        a room's model
    nab/ENEM11.NAB                       an enemy (the PC's format)
    map/MAP00_1.png                      a map screen
    text/ROOMS.txt                       the rooms' messages
    data/COMBAT.txt                      weapons, damage, hit points
    data/SCRIPTS.txt                     the rooms' event scripts
    NOISE/023/02.wav                     a sound effect
    wav/enemy/dog/snd-001.wav            the same sound by its PC name
    avi/Opening.avi                      the opening movie
```

### The mods folder

- Each folder in `mods` is one mod. They apply in alphabetical order, or in
  the order `mods/order.txt` lists them (one folder name per line). When two
  mods change the same file, the later one wins.
- A folder whose name starts with `_` is switched off.
- **F1 > Mods** switches each mod on or off (kept in `mods/disabled.txt`),
  switches plugins and the experimental bigger enemy models on or off, and
  opens the mods folder. Changes apply on the next start.
- Paths are not case-sensitive; `/` and `\` both work. Pictures can be PNG,
  BMP, JPG or TGA. Readmes and preview pictures at the top of a mod are left
  alone.

## 2. What you can mod, and how

| What | File in the mod | Notes |
| --- | --- | --- |
| Rooms: models, textures, data | `stage/STAGEnn/RRRR/...` (the PC's names) | Same files as the PC. A room can be bigger than on the PS1 (up to 1 MB): RESPeCt makes room for it |
| Scenery of stages 1, 5, 6 | `stage/STAGE01/...`, `STAGE05`, `STAGE06` | Bigger blocks make the stage's slots bigger, automatically |
| Doors, effects | `DOORNEW/DOOR_xx/DOOR_xx.HMD` and `.BIN`, `effect/*.HMD` | |
| Items, Vincent, bosses' models, enemy textures | `item/.../A.HMD`, `people/VIC700/A.HMD`, `RESOURCE/TAIRANT.HMD`, `RESOURCE/DEBUGTA00.TIM` | |
| Enemies: shape, textures, animations | `nab/ENEMnn.NAB`, `nab/ENEMnn.NAC` (the PC's format) | nn is the enemy type. Bigger than the original: switch on *Bigger enemy models* |
| An enemy's skin only | `nab/ENEMnn.png` | Drawn on the original model |
| 2D screens: maps, results, title, menus, fonts | `map/MAP00_1.png`, `RESULT/RANK_A.png`, `RESOURCE/TITLE10.png`... | A bigger picture is used as an HD version |
| **Any picture of the game**: HUD, menus, options, typewriter, results, effects, room and door textures | `PICTURES/<disc file>/<number>.png`, e.g. `PICTURES/CONPAU.BIN/011.png` (the HUD's condition box) | `extract/PICTURES` has all 2414, by file. See *Pictures* below |
| Room messages, item names and descriptions | `text/ROOMS.txt`, `text/GAME.txt`, `text/ITEMS.txt`, `text/LIST.txt`, `text/UI.txt` | Plain UTF-8: any language. See *Texts* below |
| New characters for a translation | `FONT/FONT186.png`..`188.png` + `text/CHARS.txt` | e.g. `ì 128` maps a character to a new glyph |
| The PC's message pictures | `Message/0015-00.BMP`... | 173 of them are shown in place of the PS1's text automatically, the doors' too ("It's locked.", "You've used <key>") |
| Diary pages | `diary/00/01.png` (document 0, page 1), or the PC's `diary/fileXX/...` | |
| Sound effects | `NOISE/<bank>/<n>.wav` | `extract/NOISE` has every sound by bank. A longer sound than the bank has room for still plays whole |
| The PC's sound effects by name | `wav/enemy/zombie0/snd-002.wav`, `wav/weap/gun-a-attack.wav`, `wav/door/0A-1.wav`, `wav/foot/015-0L.wav`... | Enemies, weapons, doors, footsteps, keys: to the PS1 sound of the same event. Where the PS1 has one sound for several PC files (both feet, a step shared by many rooms) the log says which one is used |
| A room's music | `NOISE/<block>/music1.seq` (sequence) or `music1.wav` (a recording) | The log says which music plays in a room |
| The PC's music by name | `BGM/012-BGM.WAV`... | 36 of the 38 tracks: every one that is a PS1 sequence, also in the sets that share it |
| Voices, streamed music, movie sound | `X/BGM_32.STR/05.wav` (channel 5), or the PC's `wav/Scene05_1.wav` | Kept in step with the game |
| A movie, frame by frame | `M/OPENING.STR/0042.png` (frame 42) | Make a `movie_dump` folder to get the original frames |
| A whole movie as a video | `avi/Opening.avi` (PC names) or `M/OPENING.STR/video.mp4` | Needs 64-bit FFmpeg DLLs next to `RESPeCt.exe` (`avformat-6x`, `avcodec-6x`, `avutil-5x/60`, `swscale-x`); Classic Rebirth's are 32-bit and don't work |
| Weapons, damage, every enemy's hit points | `data/COMBAT.txt` | See *Game data* |
| Where enemies and pickups are | `data/ROOMS.txt` | See *Game data* |
| The rooms' events (scenes, messages, sounds, flags, camera) | `data/SCRIPTS.txt` | See *Game data* |
| HD textures for everything | a texture pack (Beetle PSX HW format), see the README | F2 switches it on and off |
| The game's code | `plugins/name.dll` | See *Plugins* |

### Pictures

`extract/PICTURES` holds every picture on the disc as a PNG, in a folder per
disc file, numbered in the order they are in it: `CONPAU.BIN` is the HUD
(bullets, condition box, weapon icons, Fine/Caution/Danger, RELOADING),
`OPTTIM.BIN` the options, key and screen setup screens and the title,
`TYPE_TIM.BIN` the typewriter's letters and texts, `SCR_BG.BIN` the results
backgrounds, `EFFECT.BIN` and `EFFECTX.BIN` the effects, `B/STGBGn.BIN` the
rooms' textures. Edit one and put it in your mod under the same name.
The pictures inside the packed blocks (the enemies' and objects' textures
in `OBJ.DAT` and `EV_OBJ.DAT`, the items' in `ITEMDAT.DAT`) are in a folder
per block and part, `PICTURES/ITEMDAT.DAT/12-0/000.png`; the block is
packed again with yours.

- At the original size, the picture goes into the game's own picture,
  in its colours. Where several pictures share one palette (much of the
  HUD does), it keeps that palette: the log says so.
- Twice the size or more, it is also used as an HD version, in any colour
  (shown when the resolution is above 1x).

### Texts

`text/UI.txt` also has the doors' messages: `@DOOR_LOCKED`,
`@DOOR_LOCKED_OTHER_SIDE`, `@DOOR_UNLOCKED` and `@DOOR_USED` (the start of
"You've used the <key>.").

Each entry in a `text/*.txt` file starts with its `@` line (`@0.0` is room
block 0, message 0); edit the lines after it. Give only the entries you
change. A line break is a line break in the game. Tags: `{more}` waits for
a button, `{color 1}`/`{color 0}` colour, `{wait 60}`, `{stop}`, `{sp}`,
`{choice1}`/`{choice2}`. Keep the choice and wait tags: they can drive
events. Item names must fit the inventory row (27 codes); give a shorter
list name in `text/LIST.txt` if needed.

### Game data

`extract/data/COMBAT.txt` and `extract/data/ROOMS.txt` are plain text with a
comment above each kind of line. Change the numbers, keep only the lines
you change, and put the file in your mod.

```
rate 4 20                 # Shotgun: fire rate (the game doubles it)
shot 4 60 35 20 1         # its base damage (three values) and damage class
factor 22 100 100 150 150 150   # how much each damage class hurts type 22 (Tyrant), percent
hp 3 105                  # small enemies (zombies): hit points for placement value 3
enemy 22 1200 1200 1200 1200   # Tyrant: its four hit points (placement value 0-3, 4+: one at random)
part 0 4 0 130 100 80 80 60 60 60 60 0   # zombies, shotgun: damage percent per body part
```

`data/ROOMS.txt` lists every room's enemies and pickups (`@block`, block =
stage x 30 + room):

```
@61    # stage 2, room 1
enemy 11 289 -6368 0 -13712 3072 0 1 4   # type, game flag, x, y, z, yaw, pose, animation, hit points value
item 5 120 1000 0 -2000 0 0 0 0          # item, flag, x, y, z, rotation x y z, extra
```

A room you list gets exactly those lines: move, add or remove enemies and
pickups (at most 7 together in a room, or the original number if it had
more, up to 10).

`data/SCRIPTS.txt` has every room's event scripts, the game's own code for
its scenes: one op a line, its name and its numbers (the file's header
lists every op and how many numbers it takes):

```
@240    # stage 8, room 0
script 0
  CutsceneStart
  Message 0 1        # message 1 of this room (text/ROOMS.txt @240.1)
  Message 0 0
  ThreadEnd
```

List only the scripts you change; the others stay as they are. A longer
script than the original moves after the room's data by itself (end it
with `ThreadEnd`); a room's event data can grow to 10 KB.

### Plugins

A plugin is a 64-bit Windows DLL in `mods/<mod>/plugins/`, the way DLL mods
work on PC: it hooks any of the game's functions by name (before: may skip
it; after), calls them, reads and writes the game's memory and runs code
every frame, to change anything, from a weapon to how a zombie behaves.
[PLUGINS.md](PLUGINS.md) is the guide: a working plugin in five minutes,
the 1138 named functions (`port/include/respect_game.h`), the useful memory
and where the PC's functions are in the PS1 game. Plugins run only when the
player switches them on (F1 > Mods > Plugins), because they are programs.
A DLL made for the PC's `gun_bio.exe` doesn't run here (it patches that
program's code); the same mod is written against RESPeCt's API.

## 3. Coming from the PC version

Put a PC mod's folder in `mods` as it is, with or without `PSX_DATA`,
`Resource` and `nab`. With the whole PC installation given as one mod,
RESPeCt uses 1930 files: every one the PS1 game has an equivalent of. These
are the rooms, doors, effects, items, enemies, the bosses' models, textures,
the mapped screens, voices, movies, 36 music tracks, 790 sound effects and
162 message pictures. Loading them takes about 3 seconds at start. The log
says for each of the others what it is and what to give instead:

| PC files | Why not used | Instead |
| --- | --- | --- |
| `item/wav`, most `wav/event` sounds | the PC's one WAV for a whole event; the PS1 plays several sounds there | `NOISE/<bank>/<n>.wav`, the PS1 sound it stands for |
| the other `Message` / `eventmsg` pictures | not one of the PS1's room messages (generic ones, other languages, empty placeholders) | the text files |
| `menu/`, `Anm/*.FLC`, map cursor and frames, `item/PIC` | the PC's own screens | the PS1's pictures (in `extract`) |
| `DOORNEW/*1.HMD`, `item/ITEM_xx`, some effect and people models | models only the PC has | the PS1's models |
| `BGM/045-BGM.WAV`, `004-REAL-MOVIE.WAV` and the short sounds in `BGM` (TV, `Effect`) | no PS1 music or sound is clearly the same | `NOISE/<block>/music1.wav` or `NOISE/<bank>/<n>.wav` |
| `BGM/044-BGM.WAV`, `058-BGM-WITH-M2.WAV` next to `017-BGM`, `058-BGM-WITH-M1` | the same PS1 music as those (the better match is used) | either one alone works |
| `.RSD`, `.PLY`, `.MAT`, `.GRP`, `.BAT`... | the makers' source files | (ignored) |

`python tools/pc_mod.py check <folder>` tells beforehand which files of a
PC mod RESPeCt uses.

## 4. Sizes

The PS1 game sets memory aside for everything; RESPeCt grows it where it
can:

- A room's block up to 1 MB (the PS1: 284 KB), scenery slots up to 255
  sectors: the memory moves automatically.
- Enemy blocks up to the biggest original (186 KB packed), more with *Bigger
  enemy models* (experimental).
- A door's model up to 70 KB and its data 68 KB, an effect set 20 KB, a
  room's sound set 284 KB.
- Sound banks go whole into the sound chip's 512 KB: a sound longer than its
  bank still plays whole from your WAV.
- Pictures keep their size in the game; a bigger one becomes its HD version.
- A file inside a packed archive (items, enemy textures in `Resource`) can't
  be bigger than the original.

The log says how big each changed block is, and by how much a file is too big.

## 5. Quick saves

Memory card saves always work with or without mods. A quick save (F5)
remembers which mods were on: loading it with other mods is refused, with
the reason on screen, so it can't break. Adding or removing a mod with
bigger rooms also refuses older quick saves; load a memory card save.

## 6. When something doesn't show up

- Look in `resurv.log` for your file's `mods:` line: it says where it went
  or why not (wrong name, too big, not a file the game has).
- F1 > Mods: is the mod switched on?
- Two mods with the same file: the later one wins (`order.txt`).
- A change to a mod applies on the next start.
