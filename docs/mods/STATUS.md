# Mod support status and remaining work

Updated 2026-11-05. Branch `mod-support` (on top of `steady-3d-gpu`, PR #3).
Every row below is committed; the sections after the table are the dated
notes of how each part was checked. A passing component test does not close
the entire roadmap: the open items are listed under *Open / deliberately not
generalized*.

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
| M12a | Model/animation growth restricted to guessed per-asset allowances | Experimental default-off `dynamic_mod_buffers` manages a bounded decoded room heap, shared/instance ownership, scratch, guards and versioned state; final archive sizes indexed at boot | 23 loader groups + 13 state groups, 14 metadata assertions; actual 07/04 -> 07/00 door loaded a +400,000-byte animation payload and 616,448-byte packed block into owned memory; CPU/GPU resume, real in-flight continuation and synchronous fresh boot verified. See DYNAMIC_MOD_BUFFERS.md |
| M19 | No way to change the game's code without rebuilding RESPeCt (the PC's DLL mods) | Plugins: `mods/<mod>/plugins/*.dll` with `respect_plugin_init` (`port/include/respect_plugin.h`, `modplug.c`): hooks before/after any game function (skip, arguments, result) through `interp_call`, RAM/register access, game calls, frame callbacks. Off unless `mod_plugins = 1` | Example plugin (`port/plugins/example.c`) in real headless runs from a save: message text read, room load logged, health regen measured in RAM; off by default reported in the log. No plugin: frames and audio identical to the previous build from boot and from a save. Not yet: plugin state in quick saves, a non-Windows build |
| M21 | A room's block capped at the PS1's 290816 bytes | Over that (up to 1 MB), the room window 0x80108000-0x80150000 moves to extended RAM: native readers (`room_load.c`, `sound.c`) use `g_room_win_base/end`; the 16 TEST.BIN instructions that build the window's start or end in interpreted code (scenery slots, script block reads, the allocations past 0x800B3D54) are rewritten in the served file. Fixed scratch users (enemy files, sound banks, door model, title, typewriter, story text) stay at the old addresses. Quick saves from the other layout are refused | Headless, a 415744-byte stage 7 room 4: new game, game loads in stages 1, 5, 6, 7, 8, 14, four doors (one into the big room), quick saves in the room and mid-door: frames identical to no mod (forced move without mods: identical too). No-mod runs identical to the reference exe. Not yet: natural play, death/continue in a big room |
| M22 | Stages 1, 5, 6 scenery blocks capped at their slots (20, 12, 23 sectors) | The four area records (STAGPROG.BIN 0x800F3060/68, 0x800F3668, 0x800F328C: byte 6 slots, byte 7 sectors, read only by TEST's scenery readers) get the biggest block's size, up to 255 sectors; the room window moves, bigger by (slots + 2) x the growth | Headless: the slots 6 sectors bigger (RT_SCENERY_SLOTS), walking in stage 1 (both areas), 5 and 6: frames identical; padded stage 5 blocks (12 -> 18 sectors) render the same, only later by their longer reads. Not yet: natural play through those stages |
| M23 | The PC's loose sound effects unusable | `tools/mod_tables/mod_sfxnames.py` -> `modfs_pc_sfx.h`: 45 PC WAVs that are the PS1's own recordings (cross-correlation >= 0.94, margin 0.08) go to that bank sound and its copies | The event-verified DOG001 route found independently. Headless: the Cerberus room plays the mod's sounds by their PC names (328 of 416 notes). The other 847 PC sound files are other recordings: no automatic target |
| M24 | 7 of the PC's message pictures automatic | `tools/mod_tables/mod_msgnames.py` (Windows OCR + fuzzy match against `text/ROOMS.txt`, conflict rule for differing extra words) -> 223 room messages from 162 pictures; one decoded, cropped picture per file | Reproduces the 7 measured profiles. Headless: stage 8 room 0's notice drawn from Message/0015-00.BMP; cropped frame byte-identical to the full canvas; the whole PC set loads in 2.8 s with no picture refused by the budget |
| M25 | PS1-only 2D graphics (HUD, menus, typewriter...) editable only as whole files | `PICTURES/<file>/<n>.png`: every TIM of every non-stream disc file (2332), extracted and replaced in place with the existing converter; a palette shared with another picture is kept; 2x+ registers an HD version | Headless: the HUD's condition box (CONPAU 11) red in HD at 4x, its palette kept at 1x; another HUD picture HD. No-mod boot frames identical to the reference |
| M26 | The doors' messages neither editable nor shown as the PC's pictures | `text/UI.txt` @DOOR_LOCKED, @DOOR_LOCKED_OTHER_SIDE, @DOOR_UNLOCKED, @DOOR_USED (n_op_Door reads them through modfs_ui_text); PC pictures of them (kind GAME, by address) and of "You've used <key>" (kind USED, by key, the stream's CRC); the matcher takes exact texts, room-context rewordings | Headless, room 8/0's door: the PC's "It's now open!" in place of "You've unlocked the door."; with UI.txt, the Italian text instead. All seven measured profiles kept |
| M27 | 18 of the PC's 38 music WAVs automatic | `tools/plugins/seqrec.c` records every sequence of all 38 sets (also 2, 5, 9, 22-24, 33-37, which no room or no room table entry names), 45 s each; `tools/mod_tables/mod_bgmnames.py` looks for each PC WAV's 5 s pieces in the recordings (normalised waveform correlation, 4 kHz; score >= 0.35, 0.15 over the next, or the other half of a room pair) -> `modfs_pc_bgm.h`: 36 WAVs; byte-identical sequences in several sets (6 groups) get the WAV in each; two PC rooms' WAVs of one sequence: the better match (alt 0) wins within a mod (`pc_mod.py` marks the other duplicate) | Every old row confirmed but 002-BGM1 (was set 14, now 10) and 008-BGM-WITH-M / 002-BGM2 (now with their copies); headless: a mod with 008-WITH-M, 017, 044, 045 loads 008 into blocks 230/244/253, 017 into 241, logs 044 and 045; whole PC set 1429 files used. Left: 045-BGM (set 25 at 0.35, next 0.25), 004-REAL-MOVIE and the short TV/Effect sounds (no PS1 recording is the same) |
| M28 | 847 PC sound effects (other recordings) unusable | gun_bio is a port of the PS1 code: its WAV names carry the PS1's numbers. `tools/mod_tables/mod_sfxevents.py` -> `modfs_pc_sfx_events.h`: enemies' snd-NNN = sound NNN of the kind's bank (VAB program/tone), doors' XY-n = block 0x3C+XY sound n, footsteps' NNN-f = the PS1 room's bank 6 sound f (PC rooms 0-121 -> PS1 stage/room, read from each stageNNNN()'s 00472A10 call), keys' = block 0 sound NNN, weapons' = block id+1 (shot 5 / launchers 3, casing 1, reload 2 / handguns 3, dry fire 0); one PS1 waveform with several PC files: rank (lowest wins inside a mod, each file keeps the sound it names) | The enemy rule reproduces all 45 recorded pairs; the room table puts all 36 matched music WAVs in their room's set. 745 more files routed (158 enemy, 28 weapon, 248 door, 310 footstep, 1 key); whole PC set 1930 files used. Headless: through stage 8 room 0's door the door scene's notes are the PC files' (bench 25); 28 handgun shots + reload all the PC's (bench 26). Left: items and rooms' event sounds (PC WAVs of whole events), CHO2 (no PS1 bank) |
| M29 | The rooms' event scripts only changeable as whole ROOMENV.DAT bytes | `data/SCRIPTS.txt`: every room's script table (block part 5) as text, an op a line by name (53 named from the decompiled handlers, the rest OpNN) with its operands; lengths from TEST's table 0x800A2FC8. A listed script goes in its place if it fits (shorter: must end with ThreadEnd), else after the block's data with the table entry pointing there (in the grown copy when the block grows; a refused growth leaves the original) | 429 scripts in 126 rooms decode with no overrun (3 tails written as raw words); given back as extracted ROOMENV.DAT is identical; room 8/0's notice script made longer runs from its new place (bench 27), also when the block grows 2048 -> 4096; a 22 KB one is refused (10240 the game's limit) and the original stays. Asset-free parser test scripts-parse |
| M30 | Pictures inside packed blocks not among PICTURES | `PICTURES/<file>/<block>-<part>/<n>.png` for OBJ.DAT, EV_OBJ.DAT and ITEMDAT.DAT: the block's part unpacked (the game's LZSS), its TIMs numbered, one replaced in place with the same converter (palette sharing, HD), the block packed again (packs_done, a part nothing changed keeps the disc's packing) | 82 pictures (22, 1, 59), all of them; given back as extracted both archives identical; the zombies' texture tinted red is red in VRAM in stage 0 room 4 (bench 28) |
| M20 | Game data only changeable through code or whole binary files | `data/COMBAT.txt` (fire rate, shot damage and class, the 34 types' damage factors, the small enemies' hit points and their odds; TEST.BIN, with the RAM of older saves fixed) and `data/ROOMS.txt` (each room's enemy and pickup lists, ROOMENV.DAT, growing; at most 7 objects, or the original number up to 10) | Extracted and given back: ROOMENV.DAT and TEST.BIN byte-identical. Real headless run from a save: changed numbers in RAM, a moved enemy and an added pickup in the loaded room. Since then also `enemy` (types 11-33's four hit points) and `part` (every program's body-part factors per shot kind): the program tables (+0x840) in ENEMPROG.BIN, both slot copies; given back as extracted ENEMPROG.BIN is byte-identical, changed the Cerberus of stage 2 room 1 spawn with the new hit points. The Tyrant (type 22) spawns with the changed numbers too. Types 50+ are stage things and actors with no hit points (alive or not); stage 8 room 0's ambush zombies get 200 from STAGPROG 23. Not yet: the tables' other values, scripts and triggers |
| M31 | A quick save made before a mod plays the original sounds and music | The sound chip's memory is in the save, with the original samples: each replaced sample is also registered by its original blocks and NOISE.BIN bank, found only in SPU RAM not written since the state was loaded; one original in several banks with different PC files (gun-a/b/c/d) is told apart by the bank the game has loaded (slots at 0x800637C0). A room's music WAV silences its sequence in RAM too (a RAM fix), so the two don't play together | Headless, states saved with no mods and loaded with a PC sound mod: the handgun's shot, casing and reload are the mod's from the first shot (bench 29: 50 of 194 notes, the same sounds as a fresh load), the door 40 of 43; the user's own old quick save played the mod's footsteps and silenced the room's sequence. Played by hand with a PC-named test mod (every sound saying what it is, enemies red through the PC's NAB): all five checks passed |

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

## Continuation verification - 2026-11-03

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

## Presentation and portable workflow continuation - 2026-11-03

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

## Limits and memory gates

Status updated 2026-11-04. This preserves the audit's direct source/disc
evidence. The subsequent [explicit bitmap importer](PC_BITMAPS.md) and
[experimental model arena](DYNAMIC_MOD_BUFFERS.md) are now implemented.
The model continuation also implements selective split compressed staging
with ABI2 ownership; see DYNAMIC_MOD_BUFFERS.md for current behavior. Historical
fixed-staging limits below remain applicable to other consumers. These changes
do not provide missing semantic mappings, unlimited fixed buffers
or full-game acceptance. Historical line references below belong to the
pre-extension audit; named functions identify the current consumers.

### PC bitmap compatibility

The available PC data was audited read-only; proprietary files and
extracted pictures are not part of this repository. Counts below describe
that data set, not every localization or distribution.

#### Pickup messages are not item-check pictures

The 60 PC `item/PIC` BMPs are 512x512 pickup-message pictures (46 distinct
images). The supported PS1 disc has 45 BIGTIM item images, 112x72. They
are different resources; renaming PIC files to BIGTIM would replace the
wrong screen.

`port/decomp/item_pickup.c` constructs the original pickup
message at lines 259-300. The item-name pointer is read from
`0x800A6D5C + item_id * 4` at line 276; wait/more/end codes are added at
292-294. `port/src/modfs.c:own_picture` identifies BIGTIM separately
(lines 2352-2357). The existing UTF-8 interfaces preserve the native
message path.

Examples checked against the original table:

| PC PIC stem (hex) | Original item identity |
| --- | --- |
| 01 | Handgun B |
| 14 | First Aid Spray |
| 20 | Clock Winder |
| 29 | Prison Cell Key |
| 40 | Church Manager's Diary |

Fourteen files are the same placeholder. PIC43 and PIC46 are identical
Andy diary messages, but original item0x46 is Young Man's Diary. The later
consumer/content audit establishes45 explicit matching pictures, including
PIC43; PIC46 and the14 placeholders remain excluded. Matching numeric names
alone do not establish an identity. See PC_BITMAPS.md for current coverage.

The implemented pickup raster adapter is keyed by the original
item ID and suppresses only the corresponding visible glyphs, while still
executing the original controls. Explicit45-profile metadata and the PC_BITMAPS manifest are its interfaces;
adding aliases to `modfs_bmp.h` would not implement it. Natural pickup
acceptance remains separate from its component tests.

#### Room and event messages

The 382 available `Message` BMPs contain 308 unique pictures. Individually
checked correspondences are:

| PC stem | PS1 `text/ROOMS.txt` entry |
| --- | --- |
| 0002-01 | @0.0 |
| 0003-00 | @4.2 |
| 0003-02 | @4.3 |
| 0003-03 | @4.1 |
| 0003-04 | @4.0 |
| 0004-00 | @2.2, retaining `{wait 60}{stop}` |

The filename suffix is therefore not the PS1 message index. Duplicates
also prevent identifying every message solely by image content.

Of 137 available `eventmsg` BMPs, 136 are 1x1 pixels (135 identical black
files). Only `Re-005.bmp` contains a 512x512 message, matching ROOMS
@210.8, the Ark Thompson inscription. The other event-message identities,
segments and translated contents cannot be recovered from these
placeholders. A usable source data set or an explicit verified manifest
is required; automatic aliases must not be invented.

#### Diary document identity versus page identity

Document identity was checked from PC covers, PS1 title pages and the
original FILE_NAMES table at `0x800CF420` in the status-screen module.
The supported-disc page table and resource decoder are implemented in
`port/src/modfs_diary.inc:diary_info`, `diary_slot` and `diary_apply`.

| PC fileNN (hex) | PS1 document (decimal) | PS1 pages | Available numbered PC images |
| --- | ---: | ---: | ---: |
| 00 | 1 | 5 | 3 |
| 01 | 0 | 7 | 4 |
| 02 | 2 | 6 | 5 |
| 03 | 4 | 5 | 5 |
| 04 | 7 | 8 | 5 |
| 05 | 8 | 5 | 3 |
| 06 | 5 | 13 | 8 |
| 07 | 9 | 6 | 6 |
| 08 | 3 | 11 | 9 |
| 09 | 10 | 5 | 4 |
| 0A | 11 | 5 | 4 |
| 0B | 6 | 4 | 3 |
| 0C | 12 | 10 | 6 |

This is a **document mapping only**. Covers and text splits differ;
equal counts do not prove equal page identities. Adapt pages explicitly
to `diary/NN/PP.png`; retain the original document's page count. A future
conversion manifest needs a reviewed crop/reflow for every output page,
including titles. It cannot create readable text from absent source
images. `tools/mod_tables/mod_bmpnames.py` compares image candidates with TIMs; it
does not reconstruct glyph data, timing or pagination.

### Why general buffer growth needs a loader change

#### Addressable RAM and static mod data

`port/include/recomp.h:25-26` declares 8 MiB of RAM, with the original
2 MiB separately identified. `port/src/rt.h:ram_ptr` permits extended
addresses. This is an addressing capability, not an expanded game heap.

`port/src/modfs.c:ext_alloc` (line 1949) consumes a static prefix after
2 MiB. Only the used bytes are snapshotted in `ext_image_n` at boot.
`modfs_state_loaded` (line 411) reapplies that prefix after state load.
Mutable model, pose or animation state must never be placed in this
snapshot: reapplying it would overwrite the just-loaded state.

The present caps remain enforced: normal OBJ/EV growth targets 93 sectors
(190,464 bytes); room STGBG blocks have `OBJ_ROOM = 142*2048` (290,816
bytes); model, animation and boss growth allowances are 48, 64 and 160
KiB respectively (`modfs.c:MODEL_GROWTH`, `ANIM_GROWTH`, `BOSS_GROWTH`).
The normal 93-sector cap is not the original maximum for every boss.
Format limits such as 13,107 animation keys remain independent of RAM.

Read-only original-disc measurements:

| Archive | Blocks | Largest original compressed block |
| --- | ---: | ---: |
| OBJ.DAT | 122 | 235,520 bytes, blocks 31-33 |
| EV_OBJ.DAT | 408 | 274,432 bytes, block 402 |
| ITEMDAT.DAT | 96 | 65,536 bytes, block 94 |

For normal enemy type 18 the original packed block is 93 sectors. Its
three LZ headers declare 65,556 / 296,848 / 58,452 bytes; type 0 declares
65,556 / 259,960 / 25,608. These are payload header sizes, **not observed
peak live memory usage or proof that a larger mod fits a room**.

#### Ownership that must be preserved

| Source / function | Verified contract |
| --- | --- |
| `room_loader.c:n_loader_Begin` around 198-199 | `g_loadPtr` begins at 0x80150000; object records remain below original high-memory top |
| `room_loader.c:n_loader_RequestModel`, 215-259 | Requests data into the current destination; repeated types reuse shared data and copy work |
| `room_loader.c:n_loader_UnpackModel`, 265-297 | Decompresses into the current allocation pointer |
| `room_loader.c:n_loader_ModelRecord`, 370-423 | Record +4 animation, +8/+12 shared data, +16 instance-work base, +24 stride |
| `room_loader.c:n_loader_CopyModelWork`, 428-441 | Copies mutable work contiguously |
| `room_loader.c:n_loader_EnemyRecord`, 446-455 | Instance pointer derives from work base plus stride times instance number |
| `models.c:n_model_Bind`, 70-169 | Converts offsets to pointers and binds tracks/coordinates; final placement must precede binding |
| `room_loader.c:n_loader_Reload`, 725-741 | Rewinds according to the retained model; an unconditional arena reset is unsafe |
| `mainloop.c:n_heap_Alloc`, 230-233 | Advances the heap and compares against the original heap end |
| `enemy_spawn.c`, 116-167; `room_loader.c`, 619-711 | Pickup/item models have additional scratch-to-heap paths |
| `shooting.c:n_shadow_Init`, 488-494; `room_loader.c:n_loader_ExtraObject`, 908-918 | Shadows and extra objects also consume this heap |

Function labels in this table identify source consumers, not a promise
that all addresses in a frame are relocatable. The fixed staging buffers
`TEX_BUF=0x800F8000` and `WORK_BUF=0x80108000` are shared by texture,
streaming and audio-related paths. Redirecting only the CD callback leaves
consumers reading the old addresses (`n_cd_RequestRead`, 853-864).

Other exclusions for a first arena: `hud.c:77-78` retains a 2 MiB mask
in its length calculation; `item_check.c:128-135` uses fixed 0x800D28B0
for image/sound data. Neither becomes safe merely because DMA can address
extended RAM.

#### Arena ownership requirements from the audit

1. Reserve only the static mod prefix actually used. Place a disjoint,
   bounded dynamic room arena after it, ending before 0x80800000.
2. Preflight the whole room in 64 bits before CD reads, decompression,
   pointer changes or binding: shared payloads, every instance work copy,
   pickups, shadows and extras. Reject a mod cleanly if it cannot fit.
3. Preserve contiguous work arrays and original record/placement formats.
   Allocate at the final address before model binding; do not relocate
   an already-bound graph of pointers.
4. Make heap limits and retained-model checkpoints agree with the selected
   arena. Preserve the original path when no mod requires the arena.
5. Keep original compressed staging and SPU limits in the first increment.
   Extending those is a different ownership/timing problem.
6. Serialize arena bounds, cursor, checkpoints, owner and layout version.
   `rt.c:310-311` already saves extended RAM conditionally, but the bytes
   alone are insufficient. `savestate.c:69-84` currently tolerates missing
   general chunks; new states containing dynamic pointers must require
   their arena metadata and reject incompatibility before `SS_APPLY`.
7. Accept with no-mod state parity, multiple instances, room/return,
   retained-model reload, pickup/shadow allocation and state round trip.
   Unit allocator tests alone cannot establish gameplay safety.

The model arena now follows these ownership boundaries, with conservative
enemy preflight plus per-operation guards for later pickups/extras. It
does not claim a complete budget for every future event or live fallback
on exhaustion. See DYNAMIC_MOD_BUFFERS.md for the actual contract.

A prerequisite defect was fixed independently: `Packed.was_len` now
retains the original backup lengths, and `packs_done` checks them before
`memcmp`. The regression reproduced a larger read than that backup owns.
This correction does **not** lift any resource cap or implement an arena.

### Current decision

The bitmap mapping adapter and bounded model/animation arena are implemented
and have controlled runtime evidence. Automatic compatibility with every
PC bitmap still needs absent semantic identities and adapted pages; the
arena is experimental and remains default-off pending broader interactive
coverage. No arbitrary timing compensation or unlimited-buffer claim is
part of either feature.

#### Further PC diary consumer evidence

The owned Windows executable uses foreground format 005EA874
`diary/file%02x/file%02x_%x.bmp`; suffixes are hexadecimal. Formats 005EA89C
and 005EA8C8 address separate `_b%x` and `_b` backgrounds. Consumers 00587E40
and 00589030 compose these sets using thirteen foreground/background count pairs
at 005E9E48: (3,0),(4,0),(5,1),(5,1),(5,1),(3,1),(8,1),(6,1),(9,1),(4,1),
(4,1),(3,1),(6,3). These match the observed owned file sets. Backgrounds are
not additional text pages.

This establishes PC loading order/classes, not equal PS1 pages. A complete-
document adapter needs explicit presentation/pagination ownership and composition,
including special PS1 document4 page5/6 layouts. Partial PC diary mods also need
an original PC companion baseline or a proven adaptation; absent backgrounds/
text pages cannot be fabricated. Automatic full-document conversion remains open.


The subsequent complete-document adapter handles the contents of all 13 measured
PC diaries automatically, without asserting equal PC/PS1 page identities.
[PC_DIARIES.md](PC_DIARIES.md) documents its exact companion sets, presentation,
priority and state rules. Explicit native page/crop adaptations remain supported.
The PC labels, key atlas and book animation now have their own adapters;
see PC_DIARIES.md. Full-game and desktop-control acceptance remain open.
Historical per-page audit limits above do not describe current content coverage.

### Resident scenery bound correction (2026-11-03)

The earlier 142-sector STGBG figure is not sufficient for resident scenery.
Verified STGBG1 blocks0Ã¢â‚¬â€œ16, STGBG5 blocks2Ã¢â‚¬â€œ55 and STGBG6 blocks0Ã¢â‚¬â€œ13 have smaller
original slots; `grow_all` now enforces those bounds for nested/whole growth,
including explicit capacities. Oversized input falls back instead of being
accepted into overlapping memory. See DYNAMIC_MOD_BUFFERS.md for exact sizes,
original readers, tests and the unresolved ownership work. No general dynamic
scenery buffer support is claimed.
