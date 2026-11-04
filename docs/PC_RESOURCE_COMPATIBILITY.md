# Automatic PC asset-mod compatibility

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
