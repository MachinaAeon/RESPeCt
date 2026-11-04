"""Asset-free tests of production bitmap manifest, identity and display overlay."""
from pathlib import Path
import os,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
cc=shlex.split(os.environ.get("CC","clang"),posix=os.name!="nt")
with tempfile.TemporaryDirectory(prefix="respect-pc-bitmap-") as out:
 exe=Path(out)/("test.exe" if os.name=="nt" else "test")
 subprocess.run([*cc,"-O1","-ffunction-sections","-fdata-sections","-Wl,--gc-sections",*["-I"+str(root/p) for p in ["port/include","port/src","port/third_party","port/decomp"]],str(Path(__file__).with_suffix(".c")),str(root/"port/src/modfs_memory.c"),str(root/"port/src/modio.c"),"-o",str(exe),"-lm"],check=True)
 for name in ["automatic","automatic-invalid","automatic-priority","automatic-pages","mapping","priority","invalid","pickup","choice","signature","caption","entry","native-message","native-choice","present","diary-manifest","empty-image","ambiguous","occlusion"]:
  work=Path(out)/name;work.mkdir()
  print(name,flush=True);subprocess.run([str(exe),name],cwd=work,check=True)
