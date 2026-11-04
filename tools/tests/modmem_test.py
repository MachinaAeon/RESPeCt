"""Asset-free checks of the production room loader, arena and state validator.
CC may include target options, e.g. clang --target=x86_64-w64-mingw32.
MODMEM_TEST_OUT optionally keeps the executables in a private directory.
"""
from pathlib import Path
import os, shlex, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "clang"), posix=os.name != "nt")
def run(out):
    out = Path(out)
    for name, sources in [
        ("modmem_test", ["port/src/modmem.c", "port/src/modio.c", "port/decomp/mainloop.c", "port/decomp/shooting.c", "port/decomp/kernel_files.c", "port/decomp/enemy_spawn.c"]),
        ("modmem_state_test", ["port/src/modmem.c", "port/src/modio.c"]),
    ]:
        exe = out / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run([*cc, "-O1", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            *["-I" + str(root / p) for p in ["port/include", "port/src", "port/decomp"]],
            str(root / "tools/tests" / (name + ".c")),
            *[str(root / s) for s in sources], "-o", str(exe)], check=True)
        subprocess.run([str(exe)], cwd=out, check=True)
if os.environ.get("MODMEM_TEST_OUT"):
    out = Path(os.environ["MODMEM_TEST_OUT"]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    run(out)
else:
    with tempfile.TemporaryDirectory(prefix="respect-modmem-") as out:
        run(out)
