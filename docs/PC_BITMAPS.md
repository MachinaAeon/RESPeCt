# PC bitmap import

## Automatic verified messages

162 of the PC's message pictures load directly from a mod's `Message` or
`eventmsg` directory, including a leading `PSX_DATA/`, without a manifest: 223
PS1 room messages (a text the PS1 has in several rooms gets the picture in each).
`modfs_pc_messages.h` is the single metadata table read by runtime and importer,
written by `tools/mod_msgnames.py`: Windows' OCR of each picture against
`text/ROOMS.txt`, kept only when the closest PS1 text scores at least 0.85,
nothing different comes within 0.08, and no other PC picture with other extra
words ("card key blue" / "red") claims the same PS1 text. It reproduces the
seven profiles measured before. Pictures in other languages (some of the PC's
are Chinese), the 1x1 `eventmsg` placeholders and the PC's generic messages
that aren't room messages ("It's locked.") stay out. A picture is decoded once
for all its rows and only its visible part is kept (same frames).
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

## Authored messages, pickups and diary crops

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

## Manifest format

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

## Rendering and fallback

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

## Verified identity information and unavailable data

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

## Asset-free validation

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

## Runtime verification of this candidate

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

### Automatic-profile execution evidence (2026-11-02)

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

## Automatic pickup pictures

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

### Shared host image capacity

All93 diary resources plus45 pickups and7 ROOM profiles can load together.
This145-resource package formerly rejected18 resources at128MiB. It now passes
actual CPU/GPU loading under the shared256MiB host picture ceiling, with equal
native RAM/cycles/pixels. The budget check validates subtraction of old images
and additions before publication; overflowing or inconsistent counters refuse
allocation. Resource decode limits and original PS1 RAM/SPU capacities are
unchanged. Exhausting this finite host budget still retains prior/native art.
