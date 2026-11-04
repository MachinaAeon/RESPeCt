"""Asset-free production book loading, native phases, ownership and fallback."""
from pathlib import Path
import os,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
cc=shlex.split(os.environ.get("CC","clang"),posix=os.name!="nt")
with tempfile.TemporaryDirectory(prefix="respect-pc-book-")as temp:
    out=Path(temp);exe=out/("book.exe"if os.name=="nt"else "book")
    subprocess.run([*cc,"-O1","-ffunction-sections","-fdata-sections","-Wl,--gc-sections",*["-I"+str(root/p)for p in ("port/include","port/src","port/third_party","port/decomp")],str(Path(__file__).with_suffix(".c")),str(root/"port/src/modfs_memory.c"),str(root/"port/src/modio.c"),"-o",str(exe),"-lm"],check=True)
    subprocess.run([str(exe)],cwd=out,check=True)
