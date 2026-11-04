"""Asset-free automatic pickup identity, stream guard and native wait tests."""
from pathlib import Path
import os, shlex, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "clang"), posix=os.name != "nt")
with tempfile.TemporaryDirectory(prefix="respect-pc-pickups-") as out:
    exe = Path(out) / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([*cc, "-O1", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    *["-I" + str(root / p) for p in ["port/include", "port/src", "port/third_party", "port/decomp"]],
                    str(Path(__file__).with_suffix(".c")), str(root / "port/src/modfs_memory.c"),str(root/"port/src/modio.c"),
                    "-o", str(exe), "-lm"], check=True)
    scenarios = [("profile", i) for i in (1, 2, 3, 4, 5, 8, 11, 12, 13, 14, 15, 20, 21, 22, 23, 32, 33, 34, 36, 37, 38, 39, 41, 43, 44, 45, 46, 47, 48, 52, 53, 54, 55, 64, 65, 66, 67, 68, 69, 71, 72, 73, 74, 75, 76)] + [(s,1) for s in ("invalid","empty","unverified","guards","wait","priority")]
    for index, (name, item) in enumerate(scenarios):
        work = Path(out) / str(index); work.mkdir()
        print(name, item, flush=True)
        subprocess.run([str(exe), name, str(item)], cwd=work, check=True)
