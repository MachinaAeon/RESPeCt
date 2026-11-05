# Dynamic model and animation buffers

This is an **experimental, default-off** modding option. It expands the
room-owned model/animation heap, not every fixed PlayStation buffer:

```ini
[mods]
dynamic_mod_buffers = 1
```

Restart after changing this setting. The headless test runner accepts
`RT_MOD_ARENA=1` (or `0` to force the original path). No model-containing
mod installed means no heap redirection. An accepted whole/raw OBJ.DAT,
EV_OBJ.DAT or ITEMDAT.DAT replacement conservatively counts as such a mod,
as do supported converted model/animation changes. The option changes
where resource data lives, not enemy navigation or animation timing.

## Ownership and bounds

`modmem.c` owns a bounded arena after the static mod prefix, aligned to
16 bytes, up to 0x80800000 exclusive. `modfs_static_end()` reports only
static resources actually allocated; the prefix reapplied after loading
a state cannot overwrite mutable model state in the arena.

The native asynchronous room loader and the synchronous initial/event loader
allocate at final addresses before model binding.
Shared data remains shared, and repeated instance work stays contiguous.
Native object/placement arrays and the original 34-model-record capacity
remain in original RAM. Retained-model reload uses explicit checkpoints.
Pickups, extra objects, heap allocations and shadows check capacity before
writes. Temporary decoded textures/pickups use a bounded top-down scratch
region that cannot collide with the upward-growing heap.

At boot, `modfs_memory.c` indexes final archive sizes and split LZ header
lengths using the final TEST tables. It uses the named rebuilt archive,
including relocated archives, and publishes an index atomically. Runtime
queries do not read CD sectors or advance emulated time. Room preflight
accounts for shared payloads, contiguous copies and shadows. Later pickup
and extra-object needs are also checked immediately before their reads,
decodes and writes; the preflight is not a proof that all future events in
a room fit at once.

The continuation adds a separate persistent compressed staging lease only for
validated three-part OBJ blocks 0..29 and EV_OBJ block 406 (0x196, type 30).
It includes the trailing four-byte sentinel and remains live across CD waits
and loader steps 3..6. Texture decode scratch descends below this lease; model
heap allocation never consumes the packed reservation. Only packed source
pointers move; original TEX_BUF LoadImage/StoreImage transfers stay intact.
After part2 decode the lease releases; cancellation retains ownership while
kernel reads remain queued. Reuse/begin during pending IO fails before mutation.
Both native model-loading paths use this ownership for the identified split
resources. Other compressed readers retain their original limits.

All original CD commands, sector addresses, headers, interrupts and streaming
paths remain in use. Larger assets request their actual larger sector count;
unchanged loading time is not promised. The selective cap applies only with
this option enabled and a validated final split layout, including texture-only
changes. Whole/raw unrelated blocks, bosses, scenery, HUD and audio do not gain
this capacity. The original per-resource conversion
allowances (48/64/160 KiB) can use the extended budget only when enabled.
Format limits, including 13,107 animation keys, remain.

Insufficient capacity produces a diagnostic fatal error before the
protected operation writes outside its range. It does **not** switch a
live, already-bound graph back to the original model. Reduce the mod or
restart without it; source files and existing saves are not overwritten.

## Saves and lifecycle

States with active dynamic pointers carry ABI version 2 `mods.arena`
metadata (188 bytes), the matching mod identity and all 6 MiB of extended
RAM. Bounds, cursor, owner, checkpoints, option and RAM chunks are
validated before any running state is applied. Validation also checks the
active CDREQ table/index/sector offset and complete destination span, kernel
read destination, and every bounded queued READ, including aliases and a full
ring with equal head/tail. Missing/malformed metadata
or disabling the option makes such a state incompatible.

Earlier experimental ABI1 arena states are conservatively rejected; no opaque
pointer migration is attempted. Start from an original checkpoint or a memory
card save and load a room afresh to create an ABI2 state.

Original gameplay states with no dynamic pointers can load and clear host
arena ownership, subject to the initialization guard below. Existing bound
models are not moved. The arena starts only on
a fresh native loader begin; a new model request that requires the arena
before that boundary is explicitly refused. Start a new game, enter a new
room normally, or resume a matching arena state.

## Verification and remaining limits

Asset-free production-code checks:

```
python tools/tests/c_tests.py modmem_test
python tools/tests/c_tests.py modfs_memory_test
python tools/tests/c_tests.py modfs_test
```

An earlier bounded runtime check used a real door transition from 07/04 to
07/00 loaded two model records into the extended heap (top 0x8027F4E0).
The test NAC appended 20,000 unused commands, increasing its converted
payload from 259,960 to 339,960 bytes. Existing animation commands were
unchanged. This exceeded the old 64 KiB growth allowance; the same input
is rejected when the option is off. The resulting arena state resumed
with CPU and GPU rendering. States with stripped metadata or the option
disabled were refused before application. The starting position was a
private diagnostic pose in front of the original door trigger; it was
not a new door or a direct loader bypass.

The isolated-loader fixture tried earlier was invalid: restarting only
the object loader while rendering old models failed at the same address
with the option both on and off. It is not counted as a successful
acceptance test or as evidence of an arena-specific failure.

No-mod controlled CPU, original RAM, cycles, VRAM and display pixels match
the preserved previous build. These are bounded regression checks. A full
playthrough, natural retained-model reload, every pickup/extra/boss case,
room return and long-session memory use remain unaccepted interactively.
The option remains experimental and default-off.

Room scenery buffers, arbitrary HUD buffers, other compressed staging and SPU
memory have not become unlimited. External audio and bitmap resources
use their existing host paths; they do not imply unlimited PS1 banks or
new memory ownership for every consumer.

## Compressed staging execution evidence (2026-11-02)

An original legacy checkpoint was given only the measured position/facing in
front of a native 07/04 door. The original user save and disc tables were not
changed. The normal door state machine entered 07/00 with the owned ENEM00.NAB/
NAC mod: 100,000 unused commands were appended deterministically; original
commands/keys and NAB animation starts stayed unchanged. Decoded animation:
259,960 to 659,960 bytes; packed block: 165,888 to 616,448 bytes. The loader
completed with two model records and heap top 802CD6E0. The resulting state
resumed with CPU and actual GPU 2x. Option-off retains the original growth cap.

A real in-flight state at frame 15960 captured loader step3, CDREQ3, staging
807697F0/616,448 bytes and 289 remaining sectors. Its resumed CPU, original RAM,
extended RAM, cycles and arena metadata exactly match uninterrupted execution
at frame 16721. Removing ownership metadata from that private state is rejected
before APPLY. No-mod CPU/RAM/cycles/native VRAM/display pixels match the preceding
candidate at identical settings. Component tests additionally exercise 94/400-
sector split payloads, cancellation and invalid queued reads.

Remaining acceptance: return through rooms, retained-model loads over long
play, natural pickup/extra/boss combinations and full interactive controls.
This remains default-off and does not make every buffer unlimited.

## Synchronous initialization and state safety

The original TEST loader at 8007428C..80074AD0 is separate from the asynchronous
Begin/Run state machine. Simply enlarging asynchronous staging was insufficient:
a fresh boot with the large test mod let the original WORK_BUF packed source
overlap its animation decode destination. This corrupted the unread final part
and later stopped in the kernel. That intermediate candidate must not be used.

`n_loader_LoadSync` now owns the same bounded staging/decoded arena when the
option and model mods are active. Its C translation preserves the measured
original seven object slots, sharing rules, cached type handling after skipped
placements, raw resource read order, sound-bank calls and original CD functions.
The synchronous preflight accounts for its distinct rules before any file read.
With the option off or no model mods, it delegates to the original interpreted
function without changing registers or RAM. This is not a gameplay rewrite.

Saving and loading are deferred while this native loader is on any coroutine's
host stack. A real save requested at frame 4100 completed at frame 4361, after
the loader returned. The asynchronous loader remains saveable during CD reads.

Before applying an old state with model mods and the option enabled, validation
rejects a recognized active synchronous loader or protected fixed-buffer read.
The bounded unwinder uses verified KERNEL/TEST metadata, actual SP/RA prologues
(including delayed stack allocation in CD waits), and matching JAL callers;
it does not scan arbitrary stack words. It is not a general MIPS unwinder.
A legacy TEST startup state with an already-bound original model graph is also
rejected before application: resuming it otherwise failed at its next shadow
allocation. Start a new game, use an original gameplay checkpoint that reaches
a fresh room load, or use a matching arena state. No arbitrary old-state
migration is promised. ABI1 states still require a fresh start.

The final source-built candidate passed a complete boot through synchronous
initialization with the 616,448-byte packed fixture (three objects, one model,
heap top 802AC010), and resumed its own arena state. The original no-model
synchronous path matches the preserved interpreted control for CPU, original
RAM, cycles, native VRAM and displayed pixels. A real old startup state is
rejected before APPLY with the large mod, and loads without model mods.

The final candidate also repeated the asynchronous door and in-flight state
resume: CPU, original/extended RAM, cycles and arena metadata match the prior
verified uninterrupted run. The runtime evidence uses owned disc data and
private diagnostic fixtures, not assets embedded in source. Actual old states
captured midway inside the synchronous loader were not available; that guard
has production-code tests with verified instruction patterns, not an end-to-end
legacy mid-loader acceptance claim. Full natural gameplay acceptance remains
open and the option stays off by default.

## Resident scenery slots: verified bound, not a dynamic arena

The USA TEST reader at `80090DC4..800911CC` keeps several scenery blocks
resident, advances their addresses by `area_header[7] << 11` and reserves
`(area_header[6] + 2) * stride` before setting `g_objFileEnd` at `800B3D54`.
The later reader `80091E10..80091F98` reuses these slot addresses and submits
its own original CD search/read, outside the room-model CDREQ adapter. Its
completion is `80091F98..80092164`. The image-only reader `80092CF0..80092D44`
uses separate scratch at `800C3E50`; it is not the resident model owner.
`80092B18..80092C90` loads a NOISE bank, not a scenery block.

The measured STAGPROG headers impose these packed-block limits:

| Archive | Inclusive block range | Slot bytes | Original header / overlay |
| --- | --- | --- | --- |
| B/STGBG1.BIN | 0–11 and 12–16 | 40,960 | 800F3060 / 800F3068, overlay16 |
| B/STGBG5.BIN | 2–55 | 24,576 | 800F3668, overlay20 |
| B/STGBG6.BIN | 0–13 | 47,104 | 800F328C, overlay21 |

`modfs.c:scenery_resident_cap` clamps `grow_all` to these measured bounds.
Both nested assets and whole-block overrides are checked; an authored capacity
cannot enlarge a resident slot. Sector tables alone do not describe that
memory layout. The previous general 142-sector cap allowed overlap here.
Accepted growth up to the exact slot size remains available. Other identities
retain their previous limits, without a new safety or compatibility claim.
This guard rejects unsafe growth; it does not enlarge scenery buffers or
change original gameplay, CD commands, stage headers or slot lifetimes.

Ten synthetic production-loader scenarios cover overlap, exact boundaries,
whole blocks, explicit capacities and a separate non-resident identity. With
owned data, a padded original HMD that rebuilds STGBG1 block12 to 43,008 bytes
is refused against its 40,960-byte slot; the complete resulting archive is
byte-identical to the original. CPU, RAM, cycles and controlled display/VRAM
samples match the preceding candidate without mods and with PC diary input.
Natural traversal through each affected stage has not been tested. A general
scenery arena still requires ownership across initial/resident/reload readers,
retained model pointers, scratch reuse and save-state validation.
