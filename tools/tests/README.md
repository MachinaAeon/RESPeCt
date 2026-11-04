# Mod loader regressions

Run `python tools/tests/modfs_test.py` from the repository root. `CC` selects
an existing C compiler, e.g. `clang --target=x86_64-w64-mingw32` on Windows.
No disc, extracted game data, downloads or generated game code are needed.
Executables and fixture files live in a temporary directory.

The test includes the production loader. Allocation/copy guards stop an
out-of-bounds copy before it writes; a deliberate allocation failure checks
fallback. Synthetic archives exercise valid, oversized, integer-overflow
and custom-capacity cases. Other cases cover character-map precedence,
ordinary mapped HD pictures, empty dimensions, known resident HD textures
after state load, diary page packing/rejection, inventory row limits and
optional UI labels, compact inventory names and invalid compressed references.
Palette tests cover separate colour variants, shared font palette banks,
a nonzero transparent index, opaque STP black and equal source colours
whose indices differ in another palette. The grown packed-part fixture
checks the original backup length before production comparisons read it.
There are 48 independent loader scenario processes, including accepted/rejected
whole model archives, PC wrappers, ordered audio conversion and bounded WAV parsing.

The image decoder is a deterministic RGBA boundary substitute; the HD
registration sink records the call. These are not decoder, renderer,
sound or interactive gameplay tests. Real-disc round trips, captured
runtime pictures and state compatibility need separate integration runs;
see docs/MODDING_STATUS.md. Do not commit their proprietary fixtures.

Additional production-code suites:

```
python tools/tests/pc_bitmap_test.py
python tools/tests/modfs_memory_test.py
python tools/tests/modmem_test.py
```

They respectively exercise 19 bitmap scenarios with the real image decoder;
14 archive-index assertions; and 23 native loader/arena plus 13 state groups.
The bitmap hardware frame boundary is synthetic. Native LZ decoding,
contiguous instance copies, retained-model reload and before-write guards
are exercised, but these are not full-game or interactive-control proofs.
The pre-existing C23 label warning in shooting.c can appear in this suite.

## PC package workflow

`python tools/tests/pc_mod_test.py` exercises the standalone package tool
with generated files. It requires Python 3.9+ and no game data or compiler.
It tests path/table recognition, selected structural checks, alias conflicts,
all-or-nothing copying and preservation of source/existing destinations.
The loader suite also includes the `pc-package` dispatch regression. These
checks establish neither complete PC format compatibility nor gameplay parity.

## PC audio continuation

`python tools/tests/pc_noise_test.py` covers metadata generation and ordered
archive preflight with generated banks. Package tests also cover verified
audio aliases, no-op references, conflicts and malformed PCM. Native loader
scenarios cover atomic bank writes, VAB allocation/header rules and bounded
WAV decoding/loop traversal. These are asset-free component checks, not
listening or natural gameplay tests. See docs/PC_AUDIO_COMPATIBILITY.md.

The automatic message continuation checks measured path dispatch, exact canvas,
current-stream CRC fallback, native box/caption page limits and authored priority.
Nine event-proven DOG/CROW paths share target identities with native sound aliases.
These component tests use generated text/images; copyrighted reference text and
sound data are not embedded in tests.

Split staging tests add 94/400-sector reads through the production loader and
real LZ decoder, simultaneous packed/texture ownership, cancel/reuse, selective
identity/cap checks, relocated archive metadata and ABI2 pre-apply CD queue/span
validation. These differ from real-disc transition/in-flight save comparisons
in DYNAMIC_MOD_BUFFERS.md.


Synchronous-loader tests additionally cover its original seven-slot limit,
distinct sharing/cached-type/raw-read rules, preflight before file reads, and
exact original delegation when inactive. State tests cover fixed/delayed SP
prologues, JAL-verified caller chains, suspended threads, unsafe startup graphs
and the cross-coroutine save/load barrier. These are focused production-code
checks, not exhaustive proof of arbitrary MIPS unwinding or old-state migration.


## Complete PC diary and additional sound routes

```
python tools/tests/pc_diary_test.py
python tools/tests/pc_diary_package_test.py
python tools/tests/pc_sfx_routes_test.py
```

The diary runner has 13 independent production-code scenarios with generated
pictures, including the actual native page-load callback, complete/atomic
fallback, source layering, native/manifest/whole-archive priority, state, slide
and fractional viewport. Nine package tests read the same identity/count
metadata and enforce complete foreground/background sets before import. Five
sound-route groups verify the measured Licker aliases, including the production
C dispatch and conflicts. They are distinct from actual engine execution,
natural collection, desktop binding acceptance and listening; see PC_DIARIES.md
and PC_AUDIO_COMPATIBILITY.md for the integration evidence and limits.

## Resident scenery slot bounds

The loader runner additionally has ten `scenery-*` scenarios with synthetic
ISO directories, archives and sector tables. They demonstrate the former
overlap acceptance, now enforce the measured resident slot limits, preserve
rejected bytes/tables, accept exact boundaries and keep an unrelated identity
on its prior path. Whole overrides cannot bypass the bound with an explicit
capacity. These checks are distinct from native stage traversal and from
general dynamic scenery ownership, which are not established by this suite.

## Portable desktop importer and diary source detail

`python tools/tests/pc_mod_gui_test.py` runs nine synthetic-package tests through
the desktop backend and real package copier. It does not emulate Tk as a passing
UI test. The separate actual portable-window acceptance is described in
docs/PC_MOD_INSTALLER.md. No proprietary data or bundled interpreter is tracked.

The diary runner now has15 scenarios: `hd-detail` catches dropped odd source
columns at2x, and `area-alpha` catches transparent RGB contamination or nearest-
sample bias during1x reduction. Existing viewport, state, fallback and priority
checks remain. Runtime parity refers to native CPU/RAM/cycles; changed diary
display pixels are intentional and compared to an independent source reference.


The later D-NAME continuation adds ten title-specific cases (25 diary scenarios
total) and three package cases (12 total). Native buffer identity rather than
the updated cursor determines the title. Tests cover independent page/title
ownership, invalid/late sources, shared budget, legacy native reader state,
ambiguity, overlays, mode and full-width/widescreen drawing. Runtime evidence
and natural-navigation limits are in docs/PC_DIARIES.md.
