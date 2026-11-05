# PC version compatibility

## Product goal and current status

The goal is to import asset mods made for the Windows edition without asking
the author to convert files, write message manifests or know PS1 addresses.
That goal is **not complete**. The current candidate adds an initial package
workflow over existing converters; it does not turn every PC file into a
supported PS1 resource. Recognition, structural preflight, runtime loading
and gameplay acceptance are separate results.

Executable patches, DLLs and scripts targeting the Windows engine require
separate ports. They are not resource conversions. Original game data must
come from the user's own copies and must not be shipped with these tools.

## Current workflow

Python 3.9 or newer is required for this developer tool. No third-party
Python packages, compiler, disc or game launch are required to inspect a mod.
Run from the source checkout:

```
python tools/pc_mod.py check "path/to/pc-mod" --report "path/to/report.json"
python tools/pc_mod.py import "path/to/pc-mod" "path/to/RESPeCt/mods/my-mod"
```

A package may retain its `PSX_DATA`, `Resource` and `nab` folders. The runtime
now accepts a single leading `PSX_DATA/` wrapper and uses the same converters
as the existing flat paths. A standalone `PSX_DATA` directory can also be
inspected/imported. Standalone directories named `Resource` or `nab` retain
that identity in the imported package. Arbitrary outer wrapper folders are
not guessed; select the actual mod root.

`check` accounts for every supplied file. It reports a known loading route,
a duplicate, documentation or a blocker. Exit 0 means at least one route was
recognized and no static blocker was found; it does **not** mean the mod was
successfully decoded, fitted in memory or played. Exit 2 indicates a blocker
or invocation error. JSON reports must be new files outside the source tree.

`import` refuses a package with any blocker. It never installs just the known
fraction. It copies to a temporary disabled sibling, verifies copied hashes,
then publishes a new destination directory. Existing destinations are refused.
The source is unchanged. A compatibility-report.json accompanies the mod;
all entries remain marked runtime_verified=false. Remove the newly imported
folder to undo this installation. States made with changed mods still require
the matching resources; use the established state compatibility rules.

An offline portable graphical importer is also available; see
[PC_MOD_INSTALLER.md](PC_MOD_INSTALLER.md). It uses the same catalog and bundled
interpreter without an external Python installation. It does not automatically
enable experimental memory relocation.

## What is actually checked

The catalog reads `modfs_pc.h`, `modfs_pack.h`, `modfs_bmp.h`, `modfs_wav.h`,
`modfs_pc_sfx.h`, `modfs_pc_pickups.h`, `modfs_pc_noise.h`, `modfs_pc_messages.h` and `disc_ids.h` instead of maintaining a second list of PC filenames.
Existing native text/font/diary/item-picture/enemy/sound/stream routes are
recognized explicitly. Resource paths use the same case-insensitive identity;
unsafe components, links, reparse points, unknown files and code patches are
blocked. Alias collisions are diagnosed before copying. Numeric aliases such
as BIGTIM/1 and BIGTIM/01 address the same target. Whole archives conflict
with their known individual replacements. Byte-identical aliases with the
same complete target list are reported as duplicates and copied once.

Preflight checks non-empty files, selected image headers, complete bounded
16-bit PCM chunks, the verified PC noise profile, basic BMP layout, UTF-8 encoding, NAC size/key references, packed-part size allowances
and whole-archive sector allowances. It distinguishes the ordinary enemy NAC
trailing sentinel from the bosses' final usable key, following `nac_into`
and `enemy_whole`. It is not a second full decoder. NAB/HMD layout, native
palette compatibility, archive CRCs, final allocation, sound-bank fit and
rendering still require the production runtime and appropriate acceptance.
Some limits are room-dependent; a recognized path is not a promise of fit.

Known archive dependencies catch same-resource and whole-archive conflicts;
they are not a complete semantic conflict model for all gameplay data. NAB
and NAC companions intentionally coexist. Aliases that address shared PS1
resources may conflict even if the PC has distinct filenames.

## Evidence for this increment

- Production-loader regression: wrapped PSX_DATA text reaches the original
  consumer; unsafe/overlong paths are rejected; manifest image sources are
  excluded before wrapper dispatch. Existing loader and bitmap suites pass.
- Asset-free package tests cover wrappers, direct Resource import, collisions,
  numeric aliases, source preservation, new-destination-only installation,
  partial-import refusal, selected malformed formats and NAC boss differences.
- Three sequential real headless runs compare no mods, flat UI replacement
  and the same replacement under PSX_DATA. CPU, original RAM, cycle count,
  native VRAM and displayed pixels match their respective controls.
- A real PC map BMP was imported, decoded by the production converter and
  written into the original virtual B/MAPTIM.BIN archive. Natural map-screen
  navigation was not exercised by that boot-only resource test.

The installed executable, original disc and original saves were not changed.
No full-game, interactive-controls, audio or video acceptance is implied.

## Remaining work required for the full product goal

1. **Message and pickup BMP identities.** Seven measured ROOM profiles now load
   without a manifest, guarded against changed streams and native pagination
   (see [PC_BITMAPS.md](PC_BITMAPS.md)). Forty-five ITEM/PIC pickup profiles also have complete native stream guards.
   The remaining Message, eventmsg and item/PIC datasets are not an automatic
   PS1 message map. Most eventmsg files
   are placeholders. Identify actual Windows resource consumers and their
   corresponding PS1 event ranges before adding automatic mappings. A path
   with no verified identity stays blocked.
2. **Diary content and UI.** Complete contents of all 13 measured PC documents
   now load automatically, including covers, backgrounds and their own verified
   pagination while native controls/acquisition remain in use. See
   [PC_DIARIES.md](PC_DIARIES.md). The13 title labels also have independent automatic loading; the KEY-AB atlas and BOOK.FLC also load independently. The93 measured
   diary resources now have recognized routes;
   arbitrary partial packages without a complete baseline, natural acquisition
   and full interactive acceptance remain open; document content support does
   not supply these missing routes.
3. **Sound effects and room music.** The verified PC noise archive now has an
   ordered delta adapter, and DOG/SND-001.WAV has an independently verified event route.
   Ten former DOG/CROW/RICKER aliases were withdrawn after audit; numeric action
   IDs did not prove event identity. Explicit native NOISE paths remain available.
   See [PC_AUDIO_COMPATIBILITY.md](PC_AUDIO_COMPATIBILITY.md) for exact limits.
   Most wav/door, wav/enemy, wav/foot and BGM filenames still lack verified
   bank/event identities. Complete playback, start/stop, loops and state-cycle
   acceptance remain open; these are not supplied by a filename converter.
4. **PC-exclusive pictures and authoring resources.** Not every PC menu,
   model sidecar, alternate door mesh or movie format has a demonstrated PS1
   consumer. Separate actually loaded files from unused development files;
   implement format adapters only where their destination is established.
5. **Memory and formats.** The experimental decoded model arena and selective
   split compressed staging are not general
   host-memory loading. Other staging consumers, sound memory, fixed record counts and key
   formats remain bounded. Trace and extend each actual consumer before
   accepting assets that exceed it; retain default-off experimental behavior.
6. **Distribution and acceptance.** Package the importer for authors who do not
   have Python/a checkout; expose clear activation/priority controls; accept
   real changed assets in natural inventory, room/return, combat, sound/video
   and state cycles. Do not promote component tests into gameplay acceptance.

Completing these requires more adapters and evidence, not suppressing the
importer's blockers or relabeling them compatible. Original PS1 behavior is
the control for every loader extension.

## Portable front end (2026-11-03)

The optional source-built Windows bundle now includes its own isolated Python/Tk
runtime and the same compatibility tables. It offers a folder-based check/import
window, requires neither an installed Python nor a checkout on the author's PC,
and preserves original files and existing priority settings. See
[PC_MOD_INSTALLER.md](PC_MOD_INSTALLER.md) for the build/use recipe, actual GUI
acceptance and the observed intermittent Windows rename limitation. This closes
the basic portable-import workflow; it does not supply missing resource routes,
automatic priority management or gameplay acceptance.

## PC audio

This candidate extends automatic PC asset loading. It does not establish
complete audio-mod compatibility or listening/gameplay parity.

### Supported inputs

```
mods/my-mod/PSX_DATA/noise/noise.bin
mods/my-mod/PSX_DATA/WAV/ENEMY/DOG/SND-001.WAV
```

The existing flat paths also work. No sound assets are shipped. The feature
runs only when a mod supplies one of these files; without these files, the original PS1
path remains in use. Use `tools/pc_mod.py check` before import as documented in
[PC_RESOURCE_COMPATIBILITY.md](PC_RESOURCE_COMPATIBILITY.md).

#### The PC noise archive

The verified source archive is 16,355,328 bytes. Its reference SHA-256 is
`03ecede4126b645e9fbe0d9447c93aa9ef0ad919114ee40f7a312d4823135965`.
The supported PS1 NOISE.BIN is 16,625,664 bytes. These are different layouts:
PS1 block15 (270,336 bytes) is absent from PC; block264 has different sample
data. Ordered matching accounts for all source bytes, including duplicate
banks at distinct positions. Matching a duplicate bank at its first occurrence
would assign modifications to the wrong instance and is not used.

`modfs_pc_noise.h` contains only offsets, lengths, CRCs and allocation metadata,
not sample bytes. It describes 376 nonempty source ranges: 374 writable banks
and two retained reference-only blocks (264 and273). Block273 has no supported
editable bank shape in this adapter. The omitted PS1 block15 is never written.

For each source bank:

- An unchanged reference bank leaves the corresponding PS1 bytes intact. This
  also preserves the PS1 version of block264 and earlier mod replacements.
- A changed supported bank must retain its part offsets, VAB structural header,
  program/waveform counts and original main/sound-memory bounds. Audio tones,
  sample bytes and non-structural values can change within that contract.
- VAB size-table entry zero must remain zero, as in every audited original bank.
  The native consumer includes this entry in its address/size sum; treating it
  as unused would allow writes with invalid sample offsets or allocation.
- Any incompatible bank, source-size mismatch or changed reference-only block
  rejects the entire file before native writes. No partial bank set is applied.

The whole file is an ordered delta against the supported PC reference, rather
than a direct PS1 archive replacement. A completely unchanged PC archive is a
no-op and can coexist with individual sound replacements. Changed PC archives
and individual replacements in the same NOISE archive are conservatively
reported as conflicts by the importer; precise cross-bank merging is not
claimed. A different PC distribution needs a separately verified reference
profile; matching file size alone is insufficient.

The adapter retains existing disc sectors, file locations, CD requests and
interrupt/playback ownership. It does not enlarge the 512 KiB sound memory,
change sound events or introduce another mixer.

Metadata can be reproduced from owned original files with:

```
python tools/mod_tables/mod_noisenames.py PC/noise/noise.bin disc.bin new-profile.h
```

This tool deliberately refuses additional unknown differences or trailing bytes
and writes only a new output file. It does not modify originals or install a
new profile automatically.

#### Verified loose WAV events

`WAV/ENEMY/DOG/SND-001.WAV` targets NOISE block23, waveform2. The proof is the
original event path, not a filename/PCM similarity guess:

- PC executable `0043D630` reaches `00471260(11,1)` through its locomotion
  branch. The dispatcher constructs the DOG/SND-001 filename and loads it
  through `0057F970`.
- PS1 enemy program lookup maps kind11 to overlay51 (Cerberus), and the original
  bank table maps it to bankfile3+20=23.
- `cerberus.c:cerberus_StepSound` calls the native enemy sound with spatial
  tone12 and mono cue1. The original VAB23 maps mono1 and spatial12/13 to VAG2.

The existing converter resamples the supplied WAV to the original bank's pitch
basis and encodes it into the bounded original sample allocation. The original
SPU event, panning and playback continue to own the sound. Resampling/encoding
is not bit-identical to the source PCM and must not be described as such.
A longer sound can still be cut by the established bank budget; existing
runtime diagnostics report that limit. General unlimited sound assets remain
open.

#### PC sounds that are the PS1's recordings

`modfs_pc_sfx.h` is written by `tools/mod_tables/mod_sfxnames.py` from the PC's
`PSX_DATA` and an `extract` folder: each PC loose WAV is compared with every
bank sound (both at 11025 Hz mono, full normalized cross-correlation at the
best lag). A pair goes in only when it is the same recording: at least 0.94,
no other bank sound within 0.08, lengths within 0.8-1.25. 45 PC files
qualify: the zombies', dog's, crows', spider's, crocodile's, Ivy's and a
licker's sounds, eight key sounds and one event; the PC's door, footstep and
weapon sounds are other recordings. The method gives the event-verified
DOG/SND-001 -> block 23 sound 2 independently (0.959). A replacement goes to
that sound and its copies in the other banks. Checked in the headless
runner: in stage 2 room 1, 328 of 416 notes played were the mod's sounds
given by the PC's names (wav/enemy/dog).

#### PC sounds by their event

`modfs_pc_sfx_events.h` (`tools/mod_tables/mod_sfxevents.py`, from the PC's
`PSX_DATA`, NOISE.BIN, KERNEL.BIN and TEST.BIN) routes the other PC
recordings by the event the PC plays them for, read from gun_bio (a port
of the PS1 code, `C:\code\PC_GUN_BIO_2\PSX\`):

| PC file | gun_bio | PS1 sound |
| --- | --- | --- |
| `wav/enemy/<kind>/snd-NNN.wav` | 00471260 (kind, NNN) | the kind's bank (TEST 0x800A22BC + 0x14; zombies 30 and 33), sound NNN: program NNN >> 4, tone NNN & 15 |
| `wav/door/XY-n.wav` | 004711C0 (door scene XY, n) | block 0x3C + XY (room_change.c step 9 loads it into bank 6), sound n |
| `wav/foot/NNN-f{L,R}.wav` | 004721C0 (room NNN, floor f) | the room's bank 6, block 0x10E + 0x800A2364[stage] + room, sound f (0x800BB194) |
| `wav/key1/key-NNN.wav` | 004722B0 (NNN) | block 0, sound NNN |
| `wav/weap/<weapon>-<action>.wav` | 00472620 (weapon id, action) | block id + 1 (bank 1, 8008E324): ATTACK 5 (launchers 3), BACK 1, RELOAD 2 (handguns 3), NO 0 |

The PC rooms 0-121 are its stageNNNN() functions, each loading one PS1
stage and room through 00472A10; the table is in the tool, and all 36
matched music WAVs fall in their room's sound set. The enemy rule gives
all 45 recorded pairs above (the repeated tones too: zombie 5/7/9/11, dog
15). 745 more files: 158 enemy, 28 weapon, 248 door, 310 footstep, 1 key;
CHO2 has no PS1 bank. The weapons' actions: gun_bio plays ATTACK on a shot
(8008E368 plays sound 5; the launchers' shot is the round leaving, sound
3), BACK when the spent casing's animation ends (the PS1's clink, sound
1), RELOAD when the last round is in (the handguns' magazine-in, sound 3;
the others load one at a time with sound 2), NO on a dry fire (sound 0).
Headless: 28 handgun shots play sounds 5 and 1, the reload 2 then 3; with
the PC's weapon WAVs all of them are the mod's (bench check 26). The
rooms' event WAVs (`wav/event`) are the PC's recordings of whole events
(`EVE-ALL`, the TV's loop): no PS1 sound is the same event, so they stay
NOISE names. Where several PC files are one PS1 waveform, each goes to the
sound it names and the copies get the lowest ranked (enemies, keys,
footsteps with the left foot first, doors; by name); `pc_mod.py` marks the
others duplicate. Checked headless: through stage 8 room 0's door, the
door scene's notes are the mod's (bench check 25).

The recordings agree with the event routes below: DOG005 is bank 23's sound
5 (waveform 6), not the 12 an action number suggested.

#### Earlier withdrawn candidates

DOG006, CROW002/003 and RICKER004/007 were once withdrawn: numeric action
states had been equated across the two executables without proof that the
events match. That proof is now the PC code itself: gun_bio's enemy sound
function (00471260) builds `snd-NNN` from the sound number its callers, the
ported PS1 enemy code, pass, and the PS1 plays that number from the kind's
bank (snd_CueEnemyKind: nibbles = program, tone). Every one of the 45 pairs
found by recording follows it, repeated tones included. These files are
other recordings of those events (their sound differs from the PS1's, as
the earlier comparison found), so they go there by event, like the doors
and footsteps (`modfs_pc_sfx_events.h`).

#### Historical conversion tests are not event proof

Earlier controlled runs demonstrated that the converter could change the
then-configured eight DOG/CROW targets and two RICKER targets while preserving
unrelated waveforms. Those results establish converter isolation only. They
do not validate the semantic assignments, which have now been withdrawn.
Negative regression tests cover all ten rejected aliases, native-route
availability, the independent DOG001 alias, duplicate/conflict handling and
source preservation. No listening or full-game acceptance is claimed.

## PC message pictures

### Automatic verified messages

162 of the PC's message pictures load directly from a mod's `Message` or
`eventmsg` directory, including a leading `PSX_DATA/`, without a manifest: 223
PS1 room messages (a text the PS1 has in several rooms gets the picture in each).
`modfs_pc_messages.h` is the single metadata table read by runtime and importer,
written by `tools/mod_tables/mod_msgnames.py`: Windows' OCR of each picture against
`text/ROOMS.txt`, kept only when the closest PS1 text scores at least 0.85,
nothing different comes within 0.08, and no other PC picture with other extra
words ("card key blue" / "red") claims the same PS1 text. It reproduces the
seven profiles measured before. Pictures in other languages (some of the PC's
are Chinese), the 1x1 `eventmsg` placeholders and the PC's generic messages
that aren't room messages stay out. A picture is decoded once for all its
rows and only its visible part is kept (same frames).

Since then the tool also: takes an exact text even when the PS1 has a near
one ("books on biology" / "on bacteriology"); places a PC group in its room
by its texts only that room has, and inside that room takes the PC's
rewordings ("cable car" for "rope way") at 0.70 with 0.15 over the next;
maps the doors' own messages (TEST.BIN: "It's locked.", "It's locked from
the other side.", and "You've unlocked the door.", which the PC words "It's
now open!" in the same room's group) by their address; and the doors'
"You've used the <key>." by the key's item number (14 keys), guarded by the
CRC of the message as n_op_Door builds it. Now 234 room messages from 173
pictures, 3 game messages and 14 keys; the seven measured profiles kept.
It contains identities, halfword counts and CRCs, never message text or artwork.
The verified paths below require a 512x512 canvas. BMP/PNG/JPG/JPEG/TGA alternate
encodings share the identity; final decoding/dimensions remain runtime checks.
Missing, invalid, empty/black or incompatible pictures retain native glyphs.

Automatic profiles use the complete square PC canvas, black-key transparency,
and the centred original 320x240 output rectangle. This is the explicit PC
presentation policy; it is not a claim of pixel equality with PS1 artwork.
The runtime checks the **active** original stream CRC, so a UTF-8/raw message
change after boot cannot accidentally inherit the reference's ranges.
An automatic full-text image is used only when that complete text fits the
actual native message box on one page. Multi-page boxes/captions, choices,
ambiguous pointers and original screen overlays retain native glyphs. Thus
no later page is revealed early. Authored ranges remain available below.

Automatic rows load before explicit rows within each mod; authored valid rows
win. Later mods retain existing priority. Source bytes and relative identity
participate in quick-save validation. No timer, native control, text or
progression flag is changed. Unlisted PC paths remain unsupported until verified.

### Authored messages, pickups and diary crops

For other identities/crops, put `pc-bitmaps.txt` at the root of a mod folder,
beside its `Message`, `eventmsg`, `item`, or `PSX_DATA` directory. Original mapped
TIM pictures continue through their existing loader. For example:

```
mods/my-translation/
  pc-bitmaps.txt
  Message/my-authored-message.bmp
  item/PIC/my-authored-pickup.bmp
  diary/my-adapted-page.png
```

The manifest explicitly associates a source image/crop with an original
message, item pickup, or PS1 diary page. BMP, PNG, JPG and TGA use the
existing image decoder. No game images are distributed with the adapter.

```
mods/my-translation/
  pc-bitmaps.txt
  Message/my-authored-message.bmp
  item/PIC/my-authored-pickup.bmp
  diary/my-adapted-page.png
```

### Manifest format

One mapping per line. Blank lines and lines/comments beginning with `#`
or `;` are ignored. Tokens are separated by whitespace; source filenames
must not contain whitespace. All numbers are **decimal**, including item
IDs whose PC filenames traditionally use hexadecimal. Paths are relative
to this mod root, case-insensitive, and may use `/` or `\`. Absolute paths,
empty/dot/parent components, drive/alternate-stream paths and symlinks or
junctions below the mod root are rejected.

```
ROOM block index begin end image sx sy sw sh x y w h alpha
PICKUP item_id begin end image sx sy sw sh x y w h black
DIARY document page image sx sy sw sh
```

- `ROOM block index` is the original `text/ROOMS.txt @block.index` identity.
  The block comes from the native room loader: stage * 30 + room index.
  This also selects script captions: their original message index is
  captured by the native caption function before it draws the copied page.
- `PICKUP item_id` is the original item ID, not an inferred PIC number.
  It is captured when the native pickup function constructs its message.
  Reusing that scratch buffer for a door cannot inherit the pickup ID.
- `begin` and `end` are native cursor offsets in **halfwords**, relative to
  the original message's beginning. `begin` is included, `end` is excluded.
  Supply a reviewed range for every segment/page that should use this
  raster. These are control-stream positions, not display durations.
  Colours, line breaks, speed tags, waits and prompts each still occupy
  their original halfwords. Inspect the active native stream, including
  any UTF-8 text override, rather than counting translated BMP characters.
  When a caption advances a page, its copied cursor has the same halfword
  offset in the original stream. Its original elapsed-time/page calculation
  is unchanged.
- `sx sy sw sh` selects a source pixel rectangle, with positive width and
  height. For messages, `x y w h` selects its destination within the central
  original 320 by 240 picture, origin at the top left. Destination bounds
  must fit that picture. Widescreen keeps it centred; CPU/GPU output and
  window-size presentation scale the rectangle together.
- `alpha` retains source alpha, including opaque black. `black` explicitly
  makes exact RGB black transparent, as needed by a black-background PC
  BMP. An empty raster after this conversion is rejected.
- `DIARY` uses the existing PS1 document number and one-based page number,
  including title pages. The crop is converted/scaled into that native
  page TIM, recompressed by the existing diary importer and committed only
  when the entire original block still fits. It retains the PS1 page count.
  Multiple output pages can take different crops of one PC source. Reflow
  or composition must be authored in the source images; a filename rename
  cannot reconstruct different pagination or absent text.

For example, these rows refer to **hypothetical authored images and reviewed
cursor ranges**, not measured crops or control boundaries of the PC files:

```
# One authored 256x64 message replaces native cursor positions 0 through 39.
ROOM 4 2 0 40 Message/my-authored-message.bmp 0 0 256 64 32 160 256 64 black
# The pickup for original item 0x14 (decimal 20), with a separately reviewed range.
PICKUP 20 0 40 item/PIC/my-authored-pickup.bmp 0 0 256 64 32 160 256 64 black
# An adapted page/crop for PS1 document 0, title page 1.
DIARY 0 1 diary/my-adapted-page.png 0 0 512 512
```

Later valid rows/mods win for the matching identity and cursor range.
An invalid later row leaves an earlier usable mapping intact. Manifest
resources are loaded before ordinary files of the same mod; a native
`diary/NN/PP.png` in that mod can therefore override its manifest diary
adaptation. Referenced file contents and the manifest participate in the
mod content identity used to validate quick saves, independently of the
absolute installation path.

### Rendering and fallback

The native message/caption code still generates its glyphs and executes
all original control codes. Only sorting glyph sprites completely inside
a selected bitmap rectangle is suppressed; glyphs outside it retain the
original font. The final presentation function used by the Windows GUI,
CPU output, GPU output and headless BGRX captures composites the raster.
The native VRAM image and `display_grab_rgb` remain native diagnostic data.
The raster itself is static while a segment is active; it does not recreate
per-glyph typewriter artwork. Native typewriter execution and its duration
continue. Author crops/ranges accordingly.

Original more markers, confirmation, cancellation and timing remain in
the original engine. While a native two-way choice is active, the entire
raster path falls back to original glyphs so its blinking highlights and
answers remain visible and usable. This does not translate the choices.
Normal messages whose table entries share an indistinguishable pointer
also fall back, rather than guessing an identity. Missing mappings,
invalid/1x1/empty pictures, missing sources, unsafe paths and unsupported
display modes retain the native glyph path. During original fade, screen-dimming or white-out flags, the raster path
falls back to native glyph ordering so it cannot paint over those effects.
Other foreground occlusion and natural gameplay acceptance remain to be
checked; unit presentation tests do not establish those behaviours.

Images are prechecked at at most 4096 by 4096 before decoding. A cropped
raster may use at most 64 MiB, and all registered message rasters together
at most 256 MiB, including overridden rows. Diary native compression and
HD budget limits still apply. This is independent of the HD/F2 toggle:
message rasters are explicitly requested presentation resources, while
native diary HD promotion continues to follow the existing HD setting.

Quick saves with active bitmap resources require version 1 bitmap state:
pickup ID and caption block/index/cursor, with all three caption fields
`UINT32_MAX` when no identity is known. No host pointer or display timer
is serialized. Invalid/incompatible state is rejected before application
by the state loader integration.

### Verified identity information and unavailable data

The source audit established only these room-message correspondences:

| PC stem | Original ROOM identity |
| --- | --- |
| Message/0002-01 | 0.0 |
| Message/0003-00 | 4.2 |
| Message/0003-02 | 4.3 |
| Message/0003-03 | 4.1 |
| Message/0003-04 | 4.0 |
| Message/0004-00 | 2.2, retain native wait 60 and stop |
| eventmsg/Re-005 | 210.8 |

The seven automatic profiles have measured complete ranges [0,65), [0,68),
[0,31), [0,29), [0,46), [0,34) and [0,41), respectively. ROOM2.2 retains
its native colour changes, wait60 and stop. The full-text/single-page guard
above still applies to the actual box. No additional filename identities
are inferred.
Likewise, the measured `item/PIC` examples 01, 14, 20, 29 and 40 match
original item IDs 0x01, 0x14, 0x20, 0x29 and 0x40 respectively, but this does
not justify every PIC filename. PIC43/PIC46 include a known wrong/duplicate
semantic match. Supply explicit verified IDs in decimal.

136 of the 137 available eventmsg images are 1x1 placeholders. Their
missing translated text cannot be recovered by this importer. The diary
mapping in [MODDING_LIMITS.md](MODDING_LIMITS.md) establishes document
identity, not equal page identities. Every output page needs a reviewed
adaptation, including titles. The importer is usable for authored mappings;
**automatic compatibility with the entire PC dataset is not claimed**.

### Asset-free validation

```
python tools/tests/pc_bitmap_test.py
python tools/tests/modfs_test.py
```

The bitmap suite decodes synthetic BMPs through the real decoder, invokes
production manifest/mapping/compositing, the native message layout/update
and choice code, and the common CPU/GPU presentation function. Hardware
render output boundaries use empty synthetic frames. It also exercises an
explicit diary crop through the real LZ conversion/repack path. It does not
launch the game or prove real GPU hardware or normal gameplay acceptance.

### Runtime verification of this candidate

A diagnostic starting pose near an original 07/00 message trigger was used
to activate the original message function normally. An authored BMP marker
rendered on CPU and GPU (62,745 marker pixels in each capture); the native
message state/prompt/wait and player health matched the no-bitmap control.
A missing source produced a pixel-identical native fallback. The bitmap
message state resumed with the same manifest. An ambiguous shared-pointer
message deliberately retained its original glyphs.

An explicit DIARY BMP mapping rendered in the original diary viewer on
CPU and GPU (228,480 marker pixels each). All 90 original native diary
pages still reimported to a byte-identical CONFFILE payload. The diary
viewer state was prepared diagnostically; natural diary pickup is not
established by this check. Real PICKUP/caption acceptance and long-session
coverage remain open; their production mapping/control paths have
component tests. These results are not full-game translation acceptance.

#### Automatic-profile execution evidence (2026-11-02)

Seven actual owned PC BMPs were inspected/imported with no manifest and all
seven registered through the production loader. A private diagnostic state
with the original ROOM210.8 active message entry exercised its real native
message update and presentation: the owned Re-005 picture and a separate
procedural marker appeared on CPU and GPU. Each CPU/GPU pair is pixel-identical;
the marker has 29,298 pixels. Native message state/prompt/wait and health match
the no-bitmap control. A 4x4 invalid source retains pixel-identical native output.
This is controlled actual runtime execution, **not** a natural interaction-trigger
or diary-navigation acceptance test. Other six profiles were loaded, not each
shown naturally in its room. No full-game acceptance is claimed.


The subsequent complete-document adapter handles the contents of all 13 measured
PC diaries automatically, without asserting equal PC/PS1 page identities.
[PC_DIARIES.md](PC_DIARIES.md) documents its exact companion sets, presentation,
priority and state rules. Explicit native page/crop adaptations remain supported.
The PC labels, key atlas and book animation now have their own adapters;
see PC_DIARIES.md. Full-game and desktop-control acceptance remain open.
Historical per-page audit limits above do not describe current content coverage.

### Automatic pickup pictures

The explicit metadata-only table in modfs_pc_pickups.h covers45 measured
ITEM/PIC pictures. The original PC0045B9B0 consumer formats both PIC hex
digits from its original fourth argument. PS1 n_pickup_Step constructs
prefix/name/controls at800A88C8 and supplies two rows. Each accepted identity
was independently checked against the owned artwork, not just its number.
The remaining14 generic placeholders and mislabeled PIC46 stay unsupported.
PIC43 is Andy's diary; PIC46 repeats that artwork for Young Man's Diary and
must not inherit that identity. Importer and runtime read the same table.

Pictures require a nonempty512x512 canvas with black-key/alpha. Native item
ID, complete stream CRC and the actual single-page/two-row box must match
before native glyphs are suppressed. Authored manifest rows and later mods
retain priority. Replacing only an artwork does not replace the original
wait, confirmation, item grant or closing logic.

The original FE10 wait temporarily replaces its own halfword with FF01 until
confirmation. CRC normalization accepts only the measured suffix, savedAt
800BA9D8 immediately after it, savedHw800BA9D4 equalFE10 and exact FF01/FE00.
It never writes RAM; other text mutations or changed ownership retain glyphs.
Fifty-one generated-data scenarios cover all45 profiles and shared invalid,
empty, unverified, ownership/page/choice/lifecycle, native-wait and priority cases.

All45 owned reference pictures also render during controlled real native
message execution on CPU/GPU with equal pixels, original message state and
health. Fixture state selects the message and host ownership, so this is not
natural pickup/item-grant acceptance. Representative real confirmation/closing,
matching-state replay and missing/invalid-source fallback pass separately.

#### Shared host image capacity

All93 diary resources plus45 pickups and7 ROOM profiles can load together.
This145-resource package formerly rejected18 resources at128MiB. It now passes
actual CPU/GPU loading under the shared256MiB host picture ceiling, with equal
native RAM/cycles/pixels. The budget check validates subtraction of old images
and additions before publication; overflowing or inconsistent counters refuse
allocation. Resource decode limits and original PS1 RAM/SPU capacities are
unchanged. Exhausting this finite host budget still retains prior/native art.

## PC diaries

This candidate accepts the content of all 13 measured Windows diary sets without
a manifest. It preserves the original PS1 document identity, acquisition/list and
native page-input, slide, repeat and closing callbacks. It does not automatically
translate every inventory label or accept arbitrary PC executable modifications.
No original text, artwork or sound is distributed with the source.

### Install a document

Copy files from your own PC edition or a resource mod into a named mod folder:

```
mods/my-diary/PSX_DATA/diary/file01/File01_0.bmp
mods/my-diary/PSX_DATA/diary/file01/File01_1.bmp
mods/my-diary/PSX_DATA/diary/file01/File01_2.bmp
mods/my-diary/PSX_DATA/diary/file01/File01_3.bmp
```

The `PSX_DATA/` wrapper is optional; case is ignored. BMP, PNG, JPG/JPEG and TGA
use the same verified identities. The native ID is not guessed from the PC
number: use the table below. Foreground suffixes start at zero (the cover) and
are hexadecimal. Covers are included as the first displayed page.

| PC directory | PS1 document | Foregrounds, including cover | Backgrounds |
| --- | --- | --- | --- |
| file00 | 1 | 3 | 0 |
| file01 | 0 | 4 | 0 |
| file02 | 2 | 5 | 1 |
| file03 | 4 | 5 | 1 |
| file04 | 7 | 5 | 1 |
| file05 | 8 | 3 | 1 |
| file06 | 5 | 8 | 1 |
| file07 | 9 | 6 | 1 |
| file08 | 3 | 9 | 1 |
| file09 | 10 | 4 | 1 |
| file0a | 11 | 4 | 1 |
| file0b | 6 | 3 | 1 |
| file0c | 12 | 6 | 3 |

Foregrounds require the original 512x512 canvas; backgrounds require 512x448.
With one background the name is `FILE02_B.bmp`; with several, use the original
`FILE0C_B0.bmp`, `_B1.bmp`, `_B2.bmp`. These are backgrounds, not extra pages.
The original PC consumers at 00587E40/00589030 select the background at
`min(one-based displayed page, background count)-1`; the foreground/page-count
table is at 005E9E48 in the audited PC executable. Native document identities
were established separately, rather than equating page numbers across ports.

A complete set is validated before activation. Missing or incompatible companions
keep the entire native document (or an earlier complete mod). A later partial mod
can override individual images only if an earlier complete set supplies every
other companion. An invalid later group leaves that earlier set intact. Normal
mod order applies. A successful later native `diary/00/01.png`, explicit DIARY
mapping or whole `CONFFILE.BIN` takes priority and disables the covered automatic
book; rejected native replacements do not.

`tools/pc_mod.py check/import` reads the same metadata. A standalone package must
include its complete document; the tool cannot assume a separate PC baseline is
installed. It refuses incomplete books and unknown resources globally before
copying. This is stricter than runtime layering over an already installed complete
set, and it never silently imports only recognised files.

The13 D-NAME title pictures are supported independently as described below.
The independent KEY-AB.bmp atlas is supported as described below. BOOK.FLC is supported as described below. The measured93-file diary folder
now has routes for content, titles, keys and book animation. This does not
establish compatibility with arbitrary PC mods or the rest of PSX_DATA.

### Presentation and ownership

The entire PC foreground canvas is fitted into the original central 256x224 book
viewport; no OCR or guessed page splitting/cropping is used. Black foreground
pixels act as the PC transparency key. Original alpha is retained otherwise;
backgrounds are composed below the foreground. Native arrows and EXIT outside
the book viewport remain visible. The actually loaded page follows the native
page panel's position during a slide; changing the target page does not reveal
its image before the native load callback. Original overlays/fades take priority.
This is adapted PC artwork in the PS1 reader, not a replica of the PC menu engine.

Only the active view's page count and presentation size change for a complete
book. PS1 disc page tables, acquired-file list and story flags are not patched.
The normal native cover load still runs; external page contents do not need a
replacement CD payload. No CD command implementation, raw header, interrupt,
stream or gameplay subsystem is rewritten. Menu asset IO/timing may differ with
external pages; equal loading time is not promised. Without diary files, all
lookups are inert and original native loading runs.

Diary images and message rasters share a 256 MiB persistent host budget. A
replacement may transiently hold the previous and candidate book plus bounded
encoded/decode scratch. Each encoded file is at most 8 MiB and dimensions are
checked before decoding; profile array counts are bounded at nine foregrounds
and three backgrounds. This does not enlarge PS1 scenery, HUD, model or SPU
buffers, or make any asset size unlimited. The content mod is opt-in by presence;
removing/disabling it and restarting restores the native reader.

### Save states

New states with diary resources include version1 `mods.pc_diary` (12 bytes):
document identity and **actually loaded** page, distinct from the target in native
RAM while sliding. The current mod content/order, RAM, version, identity, count
and ranges are validated before APPLY. No host pointer, private input timer or
new acquisition flag is saved. Matching saves restore a real in-flight slide.

An old state already reading a covered native book lacks the presentation
metadata and may have different pagination. It is conservatively refused before
APPLY. Start from a gameplay/inventory checkpoint before opening the document,
or a memory-card save. An original non-reader checkpoint remains usable. Removing
metadata from an identified mod state is not a supported migration procedure.
Changing/removing mod assets retains the existing mod-identity rejection.

### Verification

```
python tools/tests/pc_diary_test.py
python tools/tests/pc_diary_package_test.py
```

Twenty-five production-code scenarios use generated pictures and archive data:
complete/missing/invalid documents, one/multiple backgrounds, ordinary native,
manifest and whole-archive priority, later partial layering, loaded-page state,
cancel/overlay, slide, fractional viewport and the native page loader, source detail and standalone labels. Twelve
package tests cover identities, companions, exact canvases, extra/wrong paths,
import and source preservation. Existing loader, bitmap, memory/state, package
and audio suites are separate regression gates.

Observed with the source-built headless runtime and owned data: all 78 content
images load, each of 13 original document-open callbacks reaches its correct
PC count/cover, CPU/GPU cover pixels match. Original right/left/Cancel/OK input
navigates a whole book and closes it; acquired-file list and story flags stay
unchanged. A real state saved while target2 still displays page1 resumes to the
same CPU, original RAM, cycles, loaded-page metadata and pixels as uninterrupted
execution. The last pages of native document4 (special PS1 layout) and document12
(multiple backgrounds) render using native page input on GPU. Missing/invalid
source groups fall back exactly to native CPU/RAM/cycles/VRAM/pixels. Actual
legacy active-reader and stripped-metadata states are rejected before APPLY.
No-mod controlled results match the preceding candidate byte-for-byte.

Document selections were prepared privately from an existing inventory state;
this is actual engine execution, not natural collection during a playthrough.
Every page of every document has not received visual acceptance. Desktop
keyboard/mouse/controller binding, Alt+Tab, full-game acquisition/return cycles
and long-session performance remain separate acceptance work. The installed
runtime was not replaced and no proprietary fixtures are committed.

### Desktop acceptance attempt (2026-11-03)

The isolated source-built GPU GUI started and quick-loaded the PC document0
cover. Two simulated Right presses, separated by a fresh focus activation,
did not change the page. The sequence stopped at this first concrete issue;
mouse/controller, return, Alt+Tab and natural pickup were not certified.
F9 did work. The Win32 frontend handles F9 through WM_KEYDOWN, while game
keys use GetAsyncKeyState polling; a brief injected pulse being missed is a
possible explanation, not an established root cause or a gameplay regression.
No input timing workaround was added. Native PS1-input navigation is covered
separately by the headless execution evidence above. Only the diagnostic
window was closed normally; installed configuration/saves were not changed.

### Full-resolution presentation correction (2026-11-03)

The earlier compositor sampled the source only at native PS1 pixel positions,
even when displaying at2x or higher. It dropped odd source columns and repeated
pixels, accentuating the indexed cover's dither pattern. `pc_diary_sample` now
integrates each displayed pixel's source footprint directly, using alpha-weighted
colours. Full source detail is retained at suitable display resolutions; smaller
views use area reduction. Transparent RGB does not introduce coloured fringes.
Native viewport, panel position, page timing, text/flags and save metadata remain
unchanged. This does not remove an original palette's dither or invent HD artwork.

Two added production-render scenarios demonstrate2x source-detail preservation
and1x alpha-weighted reduction. The initial fifteen diary scenarios pass (the title increment extends this to25). With owned diary0,
CPU/GPU captures are pixel-identical; CPU/RAM/cycles, native forward input and
in-flight slide resume match the preceding candidate. The229,376-pixel viewport
matches an independent integer area reference within one8-bit rounding step
(201 pixels differ by1); the difference is numerical rounding, not a palette
swap. The before/after cover and text page were inspected. Every document/page,
low-resolution filter preference and complete desktop controls remain unaccepted.

### Automatic PC title labels

`DIARY/D-NAME00.bmp` through `D-NAME12.bmp` are now independent optional
resources (PSX_DATA wrapper and alternate supported picture encodings work too).
Their PC IDs are decimal00..12; FILE subdirectories still use hexadecimal.
The same measured PC-to-native document table maps both. Titles require the
complete640x480 canvas, at most8MiB encoded, and share the256MiB host picture
budget. A later valid source wins; an invalid/budget-exceeding source keeps
the prior label or original native glyphs. No complete page set is required.

The native selector still emits its text and handles all input. Glyph drawing
is replaced only for the active entry at800EFD70, overlay13, file mode6,
browsing/panel state, no choices/overlay and a unique bounded match against
the13 native FILE_NAMES strings. The resolver follows that emitted buffer,
not a cursor which the same callback may already have advanced. Ambiguous,
missing or unrelated text keeps native glyphs. The full640x480 image is composed
over the central320x240 viewport using black transparency and source-detail
sampling, including the rightmost portion of D-NAME12. No host selection cache
or new saved state is introduced. Page presentation metadata remains separate;
a title-only mod does not change pagination or reject old native reader states.

Ten title-specific production scenarios cover independent labels, invalid
canvas, later priority, budget, native-state compatibility, emitted-vs-cursor
ordering, ambiguity, overlay, modes and widescreen bounds. The diary runner now
has25 scenarios; package checks have12. A real native selector emission was
captured before implementation. Controlled runs of all13 owned labels match
their full BMP canvas pixel-for-pixel; CPU/GPU agree for native IDs0 and12.
Selected-file controls, acquisition list, story flags, health and cycles match
the control. An invalid selected label restores the original entire CPU/RAM/
cycles/pixels. A matching real quick state resumes identically to uninterrupted
selector execution. Native rendering-list memory differs intentionally when
native glyphs are suppressed; complete RAM identity is not claimed for valid
title replacements.

The selector scenarios were prepared from private checkpoint copies; these
are actual native callbacks, not natural file collection or complete animated
menu navigation. KEY-AB.bmp and BOOK.FLC are now supported separately below. The full owned
diary directory reports93 recognised resources and no blocker. Recognition
remains distinct from decoding and game acceptance; blockers in other folders
are never dropped silently.

### Independent KEY-AB atlas

Supply `diary/KEY-AB.bmp` (or the PSX_DATA wrapper), exactly320x240. PNG/JPEG/TGA
encodings use the same logical name/canvas. The encoded file is limited to8MiB;
decoded keys share the256MiB picture budget. Black is transparent. Later valid
atlases replace earlier ones atomically; malformed/budget-rejected images retain
the prior atlas, or original PS1 indicators if none was accepted. No page/title
companions or adaptation manifest are required.

The PC consumers00587E40/00589030 use20x10 arrow crops atx1/22,y1/12 and20x12
EXIT crops atx1,y23(selected)/36(normal). Reader destinations are(3,84)/(297,84);
selector destinations are(69,74)/(242,74) with EXIT below at y84. The atlas is
never composited as a whole. Native blink bit3 of each original owner counter
selects the phase; there is no new timer. This preserves PS1 cadence, rather
than imposing the PC's different blink rate.

Reader ownership is the unique page callback800C6C40 with arrows800C71B8 and
mark800C7334 referencing its panel index. Only modes7/8 with panel-mode0x800
apply. During slides indicators are suppressed without stopping callbacks.
Page==count shows normal EXIT; count+1 is the original selected-exit sentinel.
Selector ownership comes from the original800CB410 initializer: arrow slots
800CF9A8/AA (800CBCFC, sides0left/1right) and EXIT slots800CF99C/9E
(800CBE20, sides0right/1left). It requires mode6, direct0 and panel-mode0x40.
Original digits, book models, callbacks, flags and input are retained. Invalid
slots, duplicate owners, unsupported display or overlays restore native drawing.
The only native-code integration is the HUD emission predicate; update logic
and RAM state are not rewritten. Host composition and suppression share guards.

Nine generated-data tests cover load/fallback/budget, ownership ambiguity,
slides/overlays, selector side inversion, exact crops and native blink, priority
and invalid later replacements. Fourteen package tests include independent
keys and canvas rejection. Real headless runs show identical no-mod CPU/RAM/
cycles/pixels, CPU/GPU key pixels, unchanged native file state and panel records,
seven actual page turns, original EXIT and exact saved-state replay. The
selector was entered through the original menu and initialized five objects
and three model records; normal selection and both sentinels were compared on
CPU/GPU. Acquired-list, model/panel state and cycles match the native control.
Valid substitutions intentionally change primitive memory and temporary CPU
values; complete modded RAM/CPU identity is not claimed. Full desktop controls,
Alt+Tab, natural document acquisition and full-game acceptance remain open.

#### Real-selector title guard correction

The initial title tests used a controlled reader-derived selector state with
panel-mode0x800. Actual menu navigation establishes that the selector uses
0x40; only the reader uses0x800. The title guard and fixtures now use0x40,
and explicitly refuse reader-mode titles. This corrects a real activation gap:
earlier synthetic title pixel comparisons did not prove normal-menu activation.
The real menu path now initializes five objects/three model records, emits the
document name and renders its owned D-NAME01 image. All3734 non-black source
pixels match the output; CPU/GPU agree and native models/panels/file state and
cycles match the control. The original input path, not a fabricated selector
callback, was used. Acquisition still originates from a controlled checkpoint.

### BOOK.FLC presentation adapter

`diary/BOOK.FLC` accepts the original196x82,18-frame,8-bit FLC layout, optionally
inside PSX_DATA. The complete encoded file is bounded to8MiB; all decoded frames
share the256MiB picture budget. The decoder validates chunk/packet/row bounds,
supports COLOR256/COLOR64, BRUN, SS2, LC, black and raw frames, skips bounded
prefix/thumbnail metadata, and validates an optional ring frame. Unsupported
image chunks or malformed files retain the complete prior/native animation.
Black pixels are transparent. The original FLC file is not rewritten.

Frame selection follows semantic phases identified in the PC consumers
00587B50..00587E34 and native file_viewer.c: closed selection uses frames0/1;
opening samples2..13 over the original31-update track; the stable open book
uses13; the original20-update forward/reverse document track samples14..17;
closing samples13..2 over the original30-update reverse track. Entry/exit
rotation phases use the closed artwork. This adapts visual frames to native
PS1 duration; it does not claim identical PC animation cadence. There is no
host timer and all original input/state/model callbacks still execute.

Ownership requires CONPAU_E overlay13, browsing/direct0, original panel-mode0x40,
five selector objects and three distinct valid model records. Both current
GsOT tag arrays are bounded before use. The original GsSortObject calls must
run because they also update animation tracks; only their newly linked draw
primitives are detached by restoring the prior tags after those calls. Native
coordinates/tracks and allocation remain intact. When ST_SORT_OLD is active,
the adapter keeps native presentation for both buffers instead of mixing an
old native list with PC artwork. Unsupported/ambiguous states fall back as a
group. No new host selection state is added to saved games.

All18 frames of the owned animation equal an independent decoder's output.
Generated-data tests cover packet overflow, palette bounds, prefixes, rings,
row skips/last pixels, old delta, truncation, ownership, native phases and
fallback. Real headless menu runs compare opening, open, document turn and
closing with the native control: mode and all three track counters match,
native file/model state is equal, and CPU/GPU presentation agrees. The first
candidate incorrectly skipped GsSortObject and stalled mode5; it was rejected
and corrected before installation. The retained native calls close that bug.

#### Sparse title work

D-NAME composition caches the alpha bounds of each actual loaded replacement,
then samples only the affected display pixels. It does not impose a fixed
original-art crop: authors may draw anywhere on the640x480 canvas. Empty and
partial-alpha margins remain transparent, with conservative footprint bounds
and widescreen clipping. A generated sparse-title regression compares its CPU
work to a dense title while verifying pixels and untouched outside canvas.

## The portable PC mod importer

The desktop importer reuses `tools/pc_mod.py` for package inspection and atomic
installation. It does not maintain a second compatibility catalog. Its Windows
bundle contains a private interpreter, standard library, Tk and the production
mapping tables. Authors do not need Python, administrator rights, network access
or a source checkout to run the resulting bundle.

This is an asset-package importer, not a mod manager or a guarantee that every
Windows resource is supported. Unknown resources block the entire package.

### Use the bundle

1. Extract the complete importer folder, keeping `runtime`, `tools` and `port`.
2. Run **RESPeCt Mod Installer.exe**.
3. Browse to the mod's source folder and the game folder containing RESPeCt.exe.
4. Choose a new mod folder name and use **Check package**. Read any blockers.
5. Use **Import into game**, then restart the game to test the mod.

The importer creates `mods/<name>` and a compatibility report inside it. Existing
destinations are refused. It never copies just the recognised fraction, replaces
original game files, starts the game or changes `order.txt`. If that file controls
mod activation, add the displayed name there yourself; the GUI reports this case.
There is no automatic reordering, disabling or save-state migration. Keep states
with the matching mod files and priority order.

The window remains responsive during inspection/copying; closing is deferred
until the current operation finishes. Failures are shown in the result area.
Changing source/game/name requires a fresh check before the import button is
enabled. The import itself always repeats validation, so changing source files
after a check does not bypass the production importer's safeguards.

### Rebuild with existing tools

The current offline recipe requires an existing complete Windows CPython3.11
installation including Tk, and an existing Clang/MinGW target toolchain. It does
not download or install them. From the checkout:

```
python tools/build_pc_mod_installer.py --output path/to/new-bundle --python-root path/to/Python311 --cc path/to/clang.exe
```

The output must be new; an existing directory is refused. It contains no game
data. The launcher invokes only its relative `runtime/pythonw.exe` and
`tools/pc_mod_gui.py`, using an isolated module path. Installed packages, user
site directories and Python environment variables are not dependencies. Source
tables are copied at build time; rebuild the bundle after changing converters
or compatibility tables. Its manifest records bundled-file SHA256 hashes.

`LICENSE` covers the importer source. CPython's bundled notices remain in
`runtime/LICENSE.txt`; Tcl/Tk notices remain under `runtime/tcl`. Do not add
game assets or the installed disc to the bundle. Compiled bundles stay outside
tracked source. Packaging for other Python versions/platforms is not established.

### Verification and limits

`python tools/tests/pc_mod_gui_test.py` uses synthetic packages and the real
inspection/copy backend. It covers safe destinations, invalid names, existing
folders, blocked files, source preservation and unchanged activation priorities.

The source-built bundle was copied outside the source checkout to a Unicode
path, then executed with empty PATH and hostile PYTHONHOME/PYTHONPATH values.
Inspection/import and existing/unknown-file refusals passed. The actual native
launcher opened the Tk window; source/game entry, check, import, refusal of an
existing destination, blocked-file display and normal closure were observed
with synthetic data. The copied file's hash matched its source. Folder-picker
dialogs, large-package progress and every Windows system have not been accepted.

One earlier diagnostic import received Windows error5 during the atomic rename;
the source remained intact and no destination was published. The same bundled
and installed interpreters then passed three controlled diagnostic imports,
and the final isolated suite plus GUI import passed. The intermittent OS error's
cause is unconfirmed. It remains a reliability limit; no silent retry/timer or
partial-copy workaround was introduced. The GUI reports OS failures so the author
can inspect the target and retry explicitly.

Successful recognition/import is not runtime decoding, memory-fit or gameplay
acceptance. Remaining asset coverage is tracked in PC_RESOURCE_COMPATIBILITY.md.
