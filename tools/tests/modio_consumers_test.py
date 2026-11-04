"""Native synchronous font/image/CD consumer tests, no game data."""
from pathlib import Path
import os,shlex,subprocess,tempfile
R=Path(__file__).resolve().parents[2]
cc=shlex.split(os.environ.get("CC","clang"),posix=os.name!="nt")
with tempfile.TemporaryDirectory(prefix="respect-modio-consumers-")as out:
 exe=Path(out)/("test.exe"if os.name=="nt"else"test")
 subprocess.run([*cc,"-O1","-ffunction-sections","-fdata-sections","-Wl,--gc-sections",*["-I"+str(R/p)for p in ["port/include","port/src","port/decomp"]],str(Path(__file__).with_suffix(".c")),*[str(R/p)for p in ["port/src/modio.c","port/decomp/message.c","port/decomp/script_ops.c","port/decomp/kernel_files.c"]],"-o",str(exe)],check=True)
 for name in ["font","image","original","queue"]:
  print(name,flush=True);subprocess.run([str(exe),name],check=True)
