"""Asset-free production KEY-AB loading, ownership, crops and fallback tests."""
from pathlib import Path
import os,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
cc=shlex.split(os.environ.get("CC","clang"),posix=os.name!="nt")
with tempfile.TemporaryDirectory(prefix="respect-diary-keys-") as tmp:
    out=Path(tmp);exe=out/("keys.exe" if os.name=="nt" else "keys")
    subprocess.run([*cc,"-O1","-ffunction-sections","-fdata-sections","-Wl,--gc-sections",*["-I"+str(root/p) for p in ("port/include","port/src","port/third_party","port/decomp")],str(Path(__file__).with_suffix(".c")),str(root/"port/src/modfs_memory.c"),str(root/"port/src/modio.c"),"-o",str(exe),"-lm"],check=True)
    for mode in ("reader","invalid","budget","ambiguous","slide","overlay","selector","crops","priority"):
        work=out/mode;work.mkdir();print(mode,flush=True)
        subprocess.run([str(exe),mode],cwd=work,check=True)
