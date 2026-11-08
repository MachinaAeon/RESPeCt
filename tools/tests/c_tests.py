"""The mod support's asset-free C tests: each test's .c is built with the
production sources it exercises and run once per scenario.

    python tools/tests/c_tests.py            every test
    python tools/tests/c_tests.py modfs_test pc_diary_test

CC may include target options, e.g. "clang --target=x86_64-w64-mingw32".
MODMEM_TEST_OUT optionally keeps the modmem executables in that directory.
"""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[2]
CC = shlex.split(os.environ.get("CC", "clang"), posix=os.name != "nt")

SRC, MODS, TABLES = "port/src", "port/src/mods", "port/src/mods/tables"
INC, THIRD, DECOMP = "port/include", "port/third_party", "port/decomp"
LOADER = ["port/src/mods/modfs_memory.c", "port/src/mods/modio.c"]
SIZE = ["-O1", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"]
STRICT = ["-std=c11", "-Wall", "-Wextra", "-Werror"]

MODFS = ['sync-tim-font-off', 'sync-tim-font-on', 'sync-tim-font-custom-off', 'sync-tim-font-malformed',
         'sync-tim-font-no-space', 'sync-tim-image-on', 'sync-tim-image-off', 'scenery-part', 'scenery-first',
         'scenery-edge', 'scenery-whole', 'scenery-whole-custom', 'scenery-stage5', 'scenery-stage5-edge',
         'scenery-stage6', 'scenery-stage6-edge', 'scenery-outside', 'split-staging-on', 'split-staging-off',
         'split-staging-malformed', 'split-staging-excluded', 'split-staging-raw', 'split-staging-ev', 'oversized',
         'overflow', 'allocation', 'valid', 'whole', 'chars', 'hd', 'hd-zero', 'diary', 'hd-state', 'long-name', 'ui',
         'lz-invalid', 'list', 'identity-order', 'hd-palettes', 'packed-growth', 'hd-font-banks', 'hd-transparency',
         'hd-black', 'hd-duplicate', 'raw-models', 'pc-package', 'scripts-parse', 'pc-audio-route',
         'pc-audio-families', 'pc-noise-identical', 'pc-noise-change', 'pc-noise-invalid', 'pc-noise-readonly',
         'pc-noise-budget', 'pc-noise-size', 'pc-noise-overlap', 'pc-noise-reference-variant', 'pc-noise-reserved',
         'pc-noise-shift', 'pc-noise-version', 'pc-noise-header-size', 'wav-format-bounds', 'wav-truncated',
         'wav-empty', 'wav-loop-truncated', 'wav-rate-range']
BITMAP = ["automatic", "automatic-invalid", "automatic-priority", "automatic-pages", "mapping", "priority", "invalid",
          "pickup", "choice", "signature", "caption", "entry", "native-message", "native-choice", "present",
          "diary-manifest", "empty-image", "ambiguous", "occlusion"]
DIARY = ["picture-budget-arithmetic", "title-shared-capacity", "title-sparse-work", "title-priority", "title-budget",
         "title-state", "title-alone", "title-invalid", "title-order", "title-ambiguous", "title-overlay",
         "title-modes", "title-wide", "hd-detail", "area-alpha", "complete", "missing", "invalid", "background",
         "native-priority", "manifest-priority", "whole-priority", "priority", "state", "cancel-fade", "slide",
         "fractional-present", "native-load"]
KEYS = ["reader", "invalid", "budget", "ambiguous", "slide", "overlay", "selector", "crops", "priority"]
PICKUPS = [["profile", str(i)] for i in (1, 2, 3, 4, 5, 8, 11, 12, 13, 14, 15, 20, 21, 22, 23, 32, 33, 34, 36, 37, 38,
                                         39, 41, 43, 44, 45, 46, 47, 48, 52, 53, 54, 55, 64, 65, 66, 67, 68, 69, 71, 72,
                                         73, 74, 75, 76)] + [[s, "1"] for s in ("invalid", "empty", "unverified",
                                                                                 "guards", "wait", "priority")]

# name: (flags, include dirs, sources besides the test, scenarios (argument lists; [] = one run with none),
#        each scenario in its own empty directory, link libm)
TESTS = {
    "modfs_test": (SIZE, [INC, SRC, MODS, TABLES, THIRD], LOADER, [[s] for s in MODFS], False, True),
    "modfs_memory_test": (STRICT, [SRC, MODS, INC], LOADER, [], False, False),
    "modio_test": (STRICT, [SRC, MODS], ["port/src/mods/modio.c"], [], False, False),
    "modio_consumers_test": (SIZE, [INC, SRC, MODS, TABLES, DECOMP],
                             ["port/src/mods/modio.c", "port/decomp/message.c", "port/decomp/script_ops.c",
                              "port/decomp/kernel_files.c"], [["font"], ["image"], ["original"], ["queue"]], False, False),
    "modmem_test": (SIZE, [INC, SRC, MODS, TABLES, DECOMP],
                    ["port/src/mods/modmem.c", "port/src/mods/modio.c", "port/decomp/mainloop.c",
                     "port/decomp/shooting.c", "port/decomp/kernel_files.c", "port/decomp/enemy_spawn.c"], [], False, False),
    "modmem_state_test": (SIZE, [INC, SRC, MODS, TABLES, DECOMP], ["port/src/mods/modmem.c", "port/src/mods/modio.c"],
                          [], False, False),
    "pc_bitmap_test": (SIZE, [INC, SRC, MODS, TABLES, THIRD, DECOMP], LOADER, [[s] for s in BITMAP], True, True),
    "pc_book_test": (SIZE, [INC, SRC, MODS, TABLES, THIRD, DECOMP], LOADER, [], False, True),
    "pc_diary_keys_test": (SIZE, [INC, SRC, MODS, TABLES, THIRD, DECOMP], LOADER, [[s] for s in KEYS], True, True),
    "pc_diary_test": (SIZE, [INC, SRC, MODS, TABLES, THIRD, DECOMP], LOADER, [[s] for s in DIARY], True, True),
    "pc_pickup_profiles_test": (SIZE, [INC, SRC, MODS, TABLES, THIRD, DECOMP], LOADER, PICKUPS, True, True),
}


def run(name, out):
    flags, incs, sources, scenarios, fresh, libm = TESTS[name]
    exe = out / (name + (".exe" if os.name == "nt" else ""))
    subprocess.run([*CC, *flags, *["-I" + str(ROOT / p) for p in incs], str(ROOT / "tools/tests" / (name + ".c")),
                    *[str(ROOT / s) for s in sources], "-o", str(exe), *(["-lm"] if libm else [])], check=True)
    failed = []
    for i, args in enumerate(scenarios or [[]]):
        work = out
        if fresh:
            work = out / ("%s-%d" % (name, i))
            work.mkdir()
        if args: print(name, *args, flush=True)
        if subprocess.run([str(exe), *args], cwd=work).returncode: failed.append(" ".join(args) or name)
    return failed


def main(names):
    bad = [n for n in names if n not in TESTS]
    if bad: raise SystemExit("no such test: " + ", ".join(bad))
    failed = []
    keep = os.environ.get("MODMEM_TEST_OUT")
    for name in names or TESTS:
        if keep and name.startswith("modmem"):
            out = Path(keep).resolve(); out.mkdir(parents=True, exist_ok=True)
            failed += run(name, out)
            continue
        with tempfile.TemporaryDirectory(prefix="respect-" + name + "-") as out:
            failed += run(name, Path(out))
    if failed: raise SystemExit("FAILED: " + ", ".join(failed))
    print("all passed")


if __name__ == "__main__":
    main(sys.argv[1:])
