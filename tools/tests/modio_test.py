"""Asset-free production synchronous resource staging tests."""
from pathlib import Path
import os,shlex,subprocess,tempfile
R=Path(__file__).resolve().parents[2]
cc=shlex.split(os.environ.get("CC","clang"),posix=os.name!="nt")
with tempfile.TemporaryDirectory(prefix="respect-modio-")as out:
 exe=Path(out)/("test.exe"if os.name=="nt"else"test")
 module=R/"port/src/modio.c"
 subprocess.run([*cc,"-std=c11","-Wall","-Wextra","-Werror","-I"+str(R/"port/src"),str(Path(__file__).with_suffix(".c")),*([str(module)]if module.exists()else[]),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
