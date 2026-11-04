"""Asset-free tests of production complete PC diary loading, state and native presentation."""
from pathlib import Path
import os,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
cc=shlex.split(os.environ.get("CC","clang"),posix=os.name!="nt")
with tempfile.TemporaryDirectory(prefix="respect-pc-bitmap-") as out:
 exe=Path(out)/("test.exe" if os.name=="nt" else "test")
 subprocess.run([*cc,"-O1","-ffunction-sections","-fdata-sections","-Wl,--gc-sections",*["-I"+str(root/p) for p in ["port/include","port/src","port/third_party","port/decomp"]],str(Path(__file__).with_suffix(".c")),str(root/"port/src/modfs_memory.c"),str(root/"port/src/modio.c"),"-o",str(exe),"-lm"],check=True)
 for name in ["picture-budget-arithmetic","title-shared-capacity","title-sparse-work","title-priority","title-budget","title-state","title-alone","title-invalid","title-order","title-ambiguous","title-overlay","title-modes","title-wide","hd-detail","area-alpha","complete","missing","invalid","background","native-priority","manifest-priority","whole-priority","priority","state","cancel-fade","slide","fractional-present","native-load"]:
  work=Path(out)/name;work.mkdir()
  print(name,flush=True);subprocess.run([str(exe),name],cwd=work,check=True)
