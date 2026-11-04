# Modding compatibility and memory extension gates

Status updated 2026-11-04. This preserves the audit's direct source/disc
evidence. The subsequent [explicit bitmap importer](PC_BITMAPS.md) and
[experimental model arena](DYNAMIC_MOD_BUFFERS.md) are now implemented.
The model continuation also implements selective split compressed staging
with ABI2 ownership; see DYNAMIC_MOD_BUFFERS.md for current behavior. Historical
fixed-staging limits below remain applicable to other consumers. These changes
do not provide missing semantic mappings, unlimited fixed buffers
or full-game acceptance. Historical line references below belong to the
pre-extension audit; named functions identify the current consumers.

## PC bitmap compatibility

The available PC data was audited read-only; proprietary files and
extracted pictures are not part of this repository. Counts below describe
that data set, not every localization or distribution.

### Pickup messages are not item-check pictures

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

### Room and event messages

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

### Diary document identity versus page identity

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
images. `tools/mod_bmpnames.py` compares image candidates with TIMs; it
does not reconstruct glyph data, timing or pagination.

## Why general buffer growth needs a loader change

### Addressable RAM and static mod data

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

### Ownership that must be preserved

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

### Arena ownership requirements from the audit

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

## Current decision

The bitmap mapping adapter and bounded model/animation arena are implemented
and have controlled runtime evidence. Automatic compatibility with every
PC bitmap still needs absent semantic identities and adapted pages; the
arena is experimental and remains default-off pending broader interactive
coverage. No arbitrary timing compensation or unlimited-buffer claim is
part of either feature.

### Further PC diary consumer evidence

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

## Resident scenery bound correction (2026-11-03)

The earlier 142-sector STGBG figure is not sufficient for resident scenery.
Verified STGBG1 blocks0Ã¢â‚¬â€œ16, STGBG5 blocks2Ã¢â‚¬â€œ55 and STGBG6 blocks0Ã¢â‚¬â€œ13 have smaller
original slots; `grow_all` now enforces those bounds for nested/whole growth,
including explicit capacities. Oversized input falls back instead of being
accepted into overlapping memory. See DYNAMIC_MOD_BUFFERS.md for exact sizes,
original readers, tests and the unresolved ownership work. No general dynamic
scenery buffer support is claimed.
