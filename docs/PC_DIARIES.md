# Automatic complete PC diaries

This candidate accepts the content of all 13 measured Windows diary sets without
a manifest. It preserves the original PS1 document identity, acquisition/list and
native page-input, slide, repeat and closing callbacks. It does not automatically
translate every inventory label or accept arbitrary PC executable modifications.
No original text, artwork or sound is distributed with the source.

## Install a document

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

## Presentation and ownership

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

## Save states

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

## Verification

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

## Desktop acceptance attempt (2026-11-03)

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

## Full-resolution presentation correction (2026-11-03)

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

## Automatic PC title labels

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

## Independent KEY-AB atlas

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

### Real-selector title guard correction

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

## BOOK.FLC presentation adapter

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

### Sparse title work

D-NAME composition caches the alpha bounds of each actual loaded replacement,
then samples only the affected display pixels. It does not impose a fixed
original-art crop: authors may draw anywhere on the640x480 canvas. Empty and
partial-alpha margins remain transparent, with conservative footprint bounds
and widescreen clipping. A generated sparse-title regression compares its CPU
work to a dense title while verifying pixels and untouched outside canvas.
