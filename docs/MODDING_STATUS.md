# Mod support status and remaining work

Updated 2026-11-04. Baseline: branch `gpu-renderer`, commit
`f054f604ab9dcb29b616b6d83832ef08cc1cdbc6`. The changes below are local,
uncommitted candidates. This file is the persistent follow-up to the
modding audit; a passing component test does not close the entire roadmap.

## Automatic PC compatibility follow-up

The full product goal is automatic PC asset-mod compatibility, rather than
requiring authors to write adaptation manifests. It remains incomplete.
The current increment accepts PSX_DATA-wrapped paths and adds an all-or-nothing
package inspection/import tool using the production mapping tables. Unknown
resources, unsafe paths and conflicting aliases block installation. Recognition
is explicitly distinct from runtime verification. See
[PC_RESOURCE_COMPATIBILITY.md](PC_RESOURCE_COMPATIBILITY.md) for workflow,
evidence and the remaining message/diary/audio/memory gates. Existing local
changes and the installed runtime are preserved; no commit/push/PR was made.

The audio continuation adds a metadata-only ordered PC noise adapter and one
verified loose-WAV event alias, with atomic rejection and original SPU bounds.
The WAV reader/importer also reject malformed PCM chunks. See
[PC_AUDIO_COMPATIBILITY.md](PC_AUDIO_COMPATIBILITY.md) for measured results
and remaining listening/gameplay acceptance. Complete PC compatibility is
still open; this is not an automatic mapping of every loose sound file.

Seven automatic message profiles have measured original-stream CRCs, exact
PC canvas validation, one-page guards and authored/later-mod priority.
ROOM210.8 is rendered by actual native execution on CPU/GPU with equal pixels
and native controls/health; invalid canvas restores the original presentation.
Ten additional loose-WAV routes previously described as verified were
withdrawn after audit: numeric action identity did not prove event identity.
Only the independently evidenced DOG001 alias remains automatic; native
NOISE paths and the ordered PC archive adapter remain available.

Selective compressed staging is now implemented with ABI2 ownership (188 bytes)
for valid OBJ0..29/EV406 split models, through both asynchronous room loading
and synchronous initialization/event loading. An actual native door test loaded a
616,448-byte packed block, resumed on CPU/GPU, and resumed during a real CD read
with 289 sectors left to exactly the uninterrupted CPU/RAM/cycle/arena result.
A fresh boot with the same large mod also completes synchronous initialization;
no-model CPU/RAM/cycles/VRAM/pixels match the original interpreted control. Saves
requested during the native synchronous loader wait until its return. Unsafe
legacy initialization states are rejected before application; arbitrary old-state
migration is not claimed. Other staging, scenery, HUD and SPU remain bounded. ABI1 experimental arena
states are rejected; the installed runtime stays untouched. Automatic complete-document content loading is described below; complete PC
asset compatibility and full-game acceptance remain open.

## Complete-document and audio continuation

Automatic content loading now covers all 13 measured PC diary sets (65
foregrounds including covers, 13 backgrounds) with whole-document validation,
shared metadata in the importer, native input/slides and versioned loaded-page
state. Controlled real opens of all 13, CPU/GPU presentation, complete-book
navigation/closing, in-flight state replay and native fallback passed. The13 title labels are now also supported; KEY-AB is now supported; BOOK.FLC is supported with native-phase presentation. See [PC_DIARIES.md](PC_DIARIES.md)
for source layout, old-reader-state restrictions and the acceptance boundary.

The current loose-WAV catalog has one independently evidenced event alias.
See PC_AUDIO_COMPATIBILITY.md for the withdrawn candidates and the stronger
evidence needed before reintroducing them. Natural playback/listening and
complete automatic PC resource compatibility remain open.

The room cap is lifted (M21): a room block past 290816 bytes moves the room
window to extended RAM. The scenery slots of stages 1, 5 and 6 grow too (M22).
The fixed scratch buffers that only borrow the old window stay as they are.

## Implemented in this candidate

| ID | Defect or gap | Change | Evidence / remaining acceptance |
| --- | --- | --- | --- |
| M01 | Archive parts copied before their final size was checked | `grow_all` preflights sizes/alignment in 64 bits, budgets custom whole-block capacities, handles allocation failure and preserves originals on rejection | Asset-free production-code regression: oversized part, integer overflow, allocation failure, valid part and failed relocation. Rejected growth is not counted as applied |
| M02 | No individual diary page interface | `modfs_diary.inc`: `diary/00/01.png`, original metadata from the owned disc, LZ decode/conversion/repack within the original block, atomic fallback; extraction of 90 pages | All 90 originals reimport to a byte-identical CONFFILE payload. One changed page observed in actual CPU and GPU runtime captures. Invalid PNG and oversized compressed replacement preserve the original payload |
| M03 | Ordinary mapped PC pictures lose HD detail | `bmp_apply` uses the same HD registration as BIGTIM; zero dimensions rejected before division; registration logs reflect actual success | Synthetic mapped-picture regression; diary HD observed with CPU and GPU. Not every map/title/result has been visually accepted |
| M04 | Overlapping custom character tokens use the first prefix | Longest match, then existing later-mod priority for equal lengths | Production text encoder regression |
| M05 | Expanded text can overflow original data structures | Enforce signed 16-bit ROOMENV offsets and 27-code inventory-row capacity; diagnose long visible names | Invalid item name rejected in regression. Visible-name wrapping/scaling is still open (M10 below) |
| M06 | Return/NO ITEM remain outside GAME translation | `text/UI.txt` adds RETURN, NO_ITEM and FULLY_LOADED in mod RAM; optional native resource lookups only | Return and NO_ITEM visibly translated in a captured run from an existing state. FULLY_LOADED encodes/loads, but the weapon-full interaction has not been exercised |
| M07 | Changed/removed mods silently resume new quick saves | New states include content and actual mod/file application order; reject a mismatch during validation, before applying state | Same mods accepted, removed/changed mods rejected; moving the root directory alone accepted. Legacy saves only warn, because their original mod set is unknown |
| M08 | Resident replacement TIMs lose HD upload history after state load | Re-register only complete rectangles whose current VRAM contents match the known replacement | Asset-free byte-identical TIM regression; runtime state resume succeeds. General migration of arbitrary resources is not claimed |
| M09 | Mod guide describes text tools as absent | MODDING.md documents UTF-8 text, fonts, UI, BIGTIM, diary pages, HD limits and state compatibility | Reviewed against the implemented paths. Underscore-prefixed mods also stay disabled when explicitly listed in order.txt |
| M10 | Full names overflow the visible inventory box | Optional `text/LIST.txt` supplies names up to 18 glyphs for the list only; full GAME strings remain intact | Asset-free lookup/limit/address-collision regression and runtime inventory capture; no automatic truncation or font scaling |
| M13 | Invalid LZ self-references read an uninitialized byte | Reject zero-distance references | Asset-free decoder regression; original diary round trip remains exact |
| M15a | HD font colours reuse one image for all palettes | One HD image per distinct CLUT; include banks shared by FONT186/187/188; retain native indices, transparency and STP black | Actual CPU/GPU inventory captures use two palette variants. Asset-free cases also cover nonzero transparent index, opaque black and duplicate colours. Final build also rendered correctly in CPU/GPU captures; see acceptance limits below |
| M18 | Grown packed parts compare beyond the original backup | Keep `was_len` and compare lengths before bytes in `packs_done` | Production-code guard reproduced a 32-byte read from a 16-byte backup before correction; the grown payload now repacks correctly |
| M11a | PC raster-message and diary import path absent | `pc-bitmaps.txt` explicitly maps ROOM/captions, PICKUP and DIARY crops; original control streams continue; native fallback for ambiguous pointers, choices, missing images and screen overlays | 15 bitmap scenarios; actual unique native message and diary BMP shown with CPU/GPU; missing-source pixels match native; bitmap state resumes. See PC_BITMAPS.md |
| M12a | Model/animation growth restricted to guessed per-asset allowances | Experimental default-off `dynamic_mod_buffers` manages a bounded decoded room heap, shared/instance ownership, scratch, guards and versioned state; final archive sizes indexed at boot | 23 loader groups + 13 state groups, 14 metadata assertions; actual 07/04â†’07/00 door loaded a +400,000-byte animation payload and 616,448-byte packed block into owned memory; CPU/GPU resume, real in-flight continuation and synchronous fresh boot verified. See DYNAMIC_MOD_BUFFERS.md |
| M19 | No way to change the game's code without rebuilding RESPeCt (the PC's DLL mods) | Plugins: `mods/<mod>/plugins/*.dll` with `respect_plugin_init` (`port/include/respect_plugin.h`, `modplug.c`): hooks before/after any game function (skip, arguments, result) through `interp_call`, RAM/register access, game calls, frame callbacks. Off unless `mod_plugins = 1` | Example plugin (`port/plugins/example.c`) in real headless runs from a save: message text read, room load logged, health regen measured in RAM; off by default reported in the log. No plugin: frames and audio identical to the previous build from boot and from a save. Not yet: plugin state in quick saves, a non-Windows build |
| M21 | A room's block capped at the PS1's 290816 bytes | Over that (up to 1 MB), the room window 0x80108000-0x80150000 moves to extended RAM: native readers (`room_load.c`, `sound.c`) use `g_room_win_base/end`; the 16 TEST.BIN instructions that build the window's start or end in interpreted code (scenery slots, script block reads, the allocations past 0x800B3D54) are rewritten in the served file. Fixed scratch users (enemy files, sound banks, door model, title, typewriter, story text) stay at the old addresses. Quick saves from the other layout are refused | Headless, a 415744-byte stage 7 room 4: new game, game loads in stages 1, 5, 6, 7, 8, 14, four doors (one into the big room), quick saves in the room and mid-door: frames identical to no mod (forced move without mods: identical too). No-mod runs identical to the reference exe. Not yet: natural play, death/continue in a big room |
| M22 | Stages 1, 5, 6 scenery blocks capped at their slots (20, 12, 23 sectors) | The four area records (STAGPROG.BIN 0x800F3060/68, 0x800F3668, 0x800F328C: byte 6 slots, byte 7 sectors, read only by TEST's scenery readers) get the biggest block's size, up to 255 sectors; the room window moves, bigger by (slots + 2) x the growth | Headless: the slots 6 sectors bigger (RT_SCENERY_SLOTS), walking in stage 1 (both areas), 5 and 6: frames identical; padded stage 5 blocks (12 -> 18 sectors) render the same, only later by their longer reads. Not yet: natural play through those stages |
| M23 | The PC's loose sound effects unusable | `tools/mod_sfxnames.py` -> `modfs_pc_sfx.h`: 45 PC WAVs that are the PS1's own recordings (cross-correlation >= 0.94, margin 0.08) go to that bank sound and its copies | The event-verified DOG001 route found independently. Headless: the Cerberus room plays the mod's sounds by their PC names (328 of 416 notes). The other 847 PC sound files are other recordings: no automatic target |
| M24 | 7 of the PC's message pictures automatic | `tools/mod_msgnames.py` (Windows OCR + fuzzy match against `text/ROOMS.txt`, conflict rule for differing extra words) -> 223 room messages from 162 pictures; one decoded, cropped picture per file | Reproduces the 7 measured profiles. Headless: stage 8 room 0's notice drawn from Message/0015-00.BMP; cropped frame byte-identical to the full canvas; the whole PC set loads in 2.8 s with no picture refused by the budget |
| M25 | PS1-only 2D graphics (HUD, menus, typewriter...) editable only as whole files | `PICTURES/<file>/<n>.png`: every TIM of every non-stream disc file (2332), extracted and replaced in place with the existing converter; a palette shared with another picture is kept; 2x+ registers an HD version | Headless: the HUD's condition box (CONPAU 11) red in HD at 4x, its palette kept at 1x; another HUD picture HD. No-mod boot frames identical to the reference |
| M20 | Game data only changeable through code or whole binary files | `data/COMBAT.txt` (fire rate, shot damage and class, the 34 types' damage factors, the small enemies' hit points and their odds; TEST.BIN, with the RAM of older saves fixed) and `data/ROOMS.txt` (each room's enemy and pickup lists, ROOMENV.DAT, growing; at most 7 objects, or the original number up to 10) | Extracted and given back: ROOMENV.DAT and TEST.BIN byte-identical. Real headless run from a save: changed numbers in RAM, a moved enemy and an added pickup in the loaded room. Since then also `enemy` (types 11-33's four hit points) and `part` (every program's body-part factors per shot kind): the program tables (+0x840) in ENEMPROG.BIN, both slot copies; given back as extracted ENEMPROG.BIN is byte-identical, changed the Cerberus of stage 2 room 1 spawn with the new hit points. The Tyrant (type 22) spawns with the changed numbers too. Types 50+ are stage things and actors with no hit points (alive or not); stage 8 room 0's ambush zombies get 200 from STAGPROG 23. Not yet: the tables' other values, scripts and triggers |

No mod means no page replacements, UI overrides or relocated text. The
candidate's no-mod controlled run matches the previous executable's CPU
state, original 2 MiB of RAM, cycle counter, native VRAM image and displayed
pixels at the compared frames. This is a regression check, not full-game
parity proof.

## Open / deliberately not generalized

| ID | Remaining requirement | Why it remains open | Next bounded step |
| --- | --- | --- | --- |
| M11b | Automatic semantic mapping of the entire PC bitmap dataset | Seven ROOM profiles are automatic; 136/137 available eventmsg are placeholders, and duplicates/diary pagination cannot supply missing identity/content | Extend verified automatic mappings for the remaining UI/message/pickup identities; complete PC document contents now have an adapter. Do not infer identities from filenames |
| M12b | General scenery/staging/HUD/audio capacity | The new decoded model heap does not relocate every fixed consumer, increase SPU RAM or remove format limits. Capacity failure is a pre-write diagnostic, not live fallback to old models | Keep unrelated staging/SPU/original record limits; independently trace any further resource class. General full-game acceptance of the experimental arena remains open |
| M14 | Complete UI/translation coverage | Only identified glyph data and mapped pictures are editable; this is not every menu string. PC image names are not all mapped | Inventory the remaining visible labels and identify their consumers before adding names |
| M15b | 24-bit TIM promotion | The native picture converter and HD entry point still reject this TIM format | Separate format support only if an actual supported-disc resource requires it |
| M16 | Arbitrary legacy-state migration or removing mods from old saves | Those states have no content identity; pointers or disc tables may already be modified | Keep the warning and use a memory card save. Do not guess which old mod set created a state |
| M17 | Longer external XA/movie tracks, enlarged original audio banks | Sequence/stream timing and SPU ownership remain original. External audio does not imply unlimited original-memory assets | Separate host-audio architecture and event/timing acceptance, if required |

## Reproducible validation

Asset-free checks:

```
python tools/tests/modfs_test.py
python tools/tests/pc_bitmap_test.py
python tools/tests/modfs_memory_test.py
python tools/tests/modmem_test.py
```

`CC` selects an existing compiler and target options. There are 48
independent loader scenario processes in the current audio continuation. Additional suites cover 19 bitmap
scenarios, 23 native-loader/arena groups, 13 state-validation groups and
14 metadata assertions. These are separate evidence types, not a count
of gameplay scenarios. The loader test substitutes image decoding and HD
registration boundaries; it does not simulate gameplay as a passing test.

No downloaded toolchain, game assets or generated game code are included.

The real-disc checks used an isolated headless release runner and private
copies of existing states. The diary rendering fixture was prepared from
an inventory state: the active command panel was set to the original file
viewer callback, document 0 was selected, then the original engine loaded
and drew the page. This proves resource loading/drawing, not natural diary
pickup/navigation in a normal playthrough. CPU and GPU captures each show
6,545 marker-coloured pixels at the same test frame. The original disc and
original saves were opened read-only.

A separate GUI candidate was subsequently opened for the authorized
interactive check, with GPU rendering and HD textures enabled. Startup,
loading the prepared diary state and drawing diary pages were observed.
The settings menu responded after Alt+Tab and focus return. No user game
was closed. The isolated candidate was paused for offline analysis, then closed normally
with Alt+F4 for sequential captures of the rebuilt candidate. Its private
executable was updated; the installed runtime was not replaced.

The input test is incomplete: short injected direction taps did not
reliably turn diary pages. Earlier page changes coincided with possible
concurrent user input, so they do not prove the automation's input was
accepted. The window polls held gameplay keys with `GetAsyncKeyState`
(`main_win32.c:key_down`), while settings also handle key messages. This
explains a possible missed-tap mechanism, not a confirmed input defect.
No input workaround was added. Inventory CHECK, movement/aim/fire and
all page turns/closing remain unaccepted.

Acceptance still required: natural diary pickup; all page turns and
closing; FULLY_LOADED; the full menu/control/Alt+Tab sequence; longer
play and sound/video coverage. Two colour variants of the HD font were
observed with CPU and GPU in actual runtime captures of the final build.
The final CPU and GPU captures are pixel-identical. Compared with the
preceding candidate, 38 displayed pixels differ within the font area,
while native VRAM is unchanged; retaining native indices resolves the
ambiguous palette assignment exercised by the duplicate-colour test.
No-mod CPU, original RAM, cycles, native VRAM and display pixels again
match the preserved baseline at the controlled frame. Audio/video
regression freedom across the whole game has not been established.

[PC_BITMAPS.md](PC_BITMAPS.md) and [DYNAMIC_MOD_BUFFERS.md](DYNAMIC_MOD_BUFFERS.md)
document the implemented manifest adapter and default-off model arena.
The final wave-3 runtime checks show native-trigger BMP display and diary
BMP import on CPU/GPU, missing-source fallback identical to native pixels,
a 90-page original diary round trip, and saved bitmap/arena continuation.
The original 64 KiB animation growth cap is retained when the option is off.
Missing arena metadata and disabling the option reject its saved state
before application. No-mod controlled CPU/RAM/cycles/VRAM/pixels again
match the preserved baseline. No complete interactive acceptance is claimed.

[MODDING_LIMITS.md](MODDING_LIMITS.md) retains the original mapping and
ownership evidence. Importing every PC file automatically and making every
fixed buffer unlimited remain outside the implemented contracts.

## Delivery and rollback

The candidate is compiled separately; the installed runtime was not
replaced. No commit, push or pull request was made. Disable/remove a mod
and start from the original game or a memory card save to roll back its
resources. New quick saves with that mod are intentionally rejected until
the matching source files and priority are restored. A source rollback
must restore the UI lookup consumers and loader together; use the Git
diff rather than mixing individual candidate objects into an older build.

## Final review corrections

The read-only review found two address/order edge cases, each reproduced
in a regression before correction: compact labels now match only current
GAME pointers (old addresses can be reused by another string); state
identities now include the actual file application order rather than a
commutative sum. Enumeration changes may therefore refuse a state
conservatively. This preserves the existing file application semantics;
it does not promise cross-filesystem ordering of overlapping aliases.

## Continuation verification â€” 2026-11-03

- Complete content for all13 mapped PC diaries remains supported; the15
  separate diary UI/animation resources remain unsupported.
- Corrected unsafe STGBG growth into measured resident slots; ten new
  production-loader regressions and all nine component suites pass. Real
  owned-data rejection preserves the entire archive byte-for-byte. This is
  a memory-bound guard, not general scenery-buffer expansion.
- A source-built diagnostic GUI started with actual GPU rendering and loaded
  a PC diary cover. The simulated Right key did not turn the page, including
  one retry after focus activation. The interactive sequence stopped there.
  Keyboard/mouse, Alt+Tab and listening acceptance are incomplete. Native
  PS1 input navigation passed separately in actual headless engine runs.
- Additional audio aliases still require event-proven mappings; natural
  playback/listening, acquisition and long play remain open. No installed
  runtime replacement or complete PC-mod compatibility is established.

## Presentation and portable workflow continuation â€” 2026-11-03

- PC diary presentation now samples full source detail at the actual output
  resolution with alpha-weighted area reduction. The former native-resolution
  repetition was reproduced before correction;15 diary scenarios pass. Owned
  CPU/GPU cover pixels agree, with native CPU/RAM/cycles/input and slide resume
  unchanged. Original indexed artwork may still contain visible dithering.
- A separate portable Windows Mod Installer reuses the strict package engine.
  It runs with its bundled interpreter/Tk and metadata, without installed Python
  or a source checkout. Nine backend tests, detached/isolated bundle execution
  and actual GUI check/import/refusal/closure passed. An intermittent earlier
  Windows atomic-rename failure remains documented, not explained away.
- No new audio alias was established: PC RICKER001/009 can be layered/shared;
  existing evidence does not isolate their native targets. No speculative route
  was added. Natural playback/listening still requires acceptance.
- The13 D-NAME UI identities now have an adapter gated by the actual native
  text entry, including the frame where the cursor changes. KEY-AB now has a verified reader/selector presentation adapter; BOOK.FLC
  remains a separate UI blocker. General scenery ownership, additional
  message/pickup routes and full-game controls remain open.

The installed game executable/configuration/saves remain unchanged. This is a
local source-built candidate, not a release or complete PC mod compatibility.


## Verified title-label increment

The D-NAME adapter now resolves the entry actually emitted, preserves native
selection/acquisition, and fits the entire640x480 picture. All13 controlled
native selector outputs match their owned BMPs; actual CPU/GPU0/12, original
fallback and real state-resume checks passed. At that stage the diary package had92 recognised files and1 blocker
(BOOK.FLC); the subsequent book increment below closes that resource blocker.
The then-current25 diary,14 package and9 key scenarios passed. These figures refer to resource coverage and controlled native
execution, not a complete playthrough or desktop acceptance. See PC_DIARIES.md.

## KEY-AB and corrected audio contract

The standalone320x240 atlas now owns only the reader arrows/page mark and the
selector arrows/EXIT panels. Original HUD callbacks, panel allocation, blink,
selection, acquisition, page count, slides and input remain in use. Reader
panel-mode0x800 and selector panel-mode0x40 are distinct, verified by actual
menu entry; invalid/ambiguous owners fall back as a group. No renderer or
enemy/gameplay changes are introduced. See PC_DIARIES.md for acceptance.

All11 mod-related regression suites pass. Actual runtime comparisons cover
native no-mod identity, CPU/GPU keys, seven page changes and EXIT, matching
real saved-state replay, and an original-initialized selector with five
objects/three model records, normal selection and both EXIT sentinels.
This is not full desktop-input or natural acquisition acceptance. BOOK.FLC,
further messages/pickups/audio and general resident-buffer growth remain open.

The same actual-menu check corrected the D-NAME guard from reader-mode0x800
to selector-mode0x40. Previous synthetic title comparisons did not establish
normal menu activation. The corrected guard passes a real menu sequence with
original model initialization; all3734 non-black owned title pixels match on
CPU/GPU, with unchanged file/panel/model state and cycles.

## Book animation and automatic pickups continuation

BOOK.FLC now has a bounded indexed decoder and a presentation adapter tied
to original native phases, while GsSortObject still runs all model animation
updates. CPU/GPU menu comparisons match the native file/model state and track
countdowns across opening/open/page-turn/closing. The measured diary folder
has93 recognized resources; that coverage figure refers only to that folder.
The automatic ITEM/PIC adapter now has45 explicitly verified mappings and
the original confirmation-mutation CRC guard. See PC_DIARIES.md and PC_BITMAPS.md.
Sparse D-NAME composition now skips transparent margins without changing art.

Audio remains limited to proven mappings; the audit found that the PC plays
layered DOG sounds where PS1 separates death, damage and timed impact. Numeric
animation IDs cannot assign the individual layers. General resident-buffer
growth is not enabled: original stride headers are mutable, but shared
font/image/music scratch, optional NOISE prefetch and pre-APPLY saved-state
ownership must be integrated together. Existing caps/ABI remain authoritative.
No detached helper or arithmetic capacity is counted as runtime support.

## Complete measured pickup artwork and shared host image capacity

The60 owned ITEM/PIC references contain45 distinct, semantically matching
pictures,14 repeated placeholders and one mislabeled duplicate (PIC46).
PC0045B9B0 formats the source path from the original item argument; each
accepted identity was also checked against the original PS1 item name and
reference artwork. PIC43 correctly identifies Andy's diary and is now supported;
PIC46 labels Young Man's Diary incorrectly and stays excluded. The table is
explicit; no arbitrary numeric alias or identity is inferred for placeholders.

All45 profiles rendered their owned picture during controlled real native
message execution on CPU/GPU. Complete native stream CRCs, two-row ownership,
wait/prompt state and health match the no-bitmap control. The test fixture
prepared message/host ownership; it did not naturally collect45 objects or
prove inventory grants. A separate representative test covers real confirmation,
closing, saved-state continuation and missing/invalid fallback. Generated-data
tests cover51 pickup scenarios and the importer's45 routes/15 exclusions.

Loading all93 diary resources,45 pickups and7 ROOM profiles together exposed
an actual host image-capacity defect: the former128MiB budget rejected18 of145
resources. A single checked256MiB persistent host budget now covers all six
image-loader consumers, with bounded replacement accounting and overflow refusal.
The combined145-resource package loads with no skipped resource on CPU/GPU;
pixels, native RAM and cycles agree. Exact-boundary/replacement/overflow tests
pass. Per-file/decode limits and allocation-failure fallback remain in place.
This is host picture storage, not a general increase of original PS1/SPU RAM.

All14 automatic suites pass for this increment. No-mod CPU/RAM/cycles/VRAM/pixels
and the previously verified opening/open/turn/closing book cases are unchanged.
The installed executable remains unchanged. Audio event associations, resident
scenery ownership and complete desktop gameplay acceptance still remain open;
these picture results do not close full automatic PC-mod compatibility.
