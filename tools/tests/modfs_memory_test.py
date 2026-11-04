from pathlib import Path
import os,shlex,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
cc=shlex.split(os.environ.get("CC","clang"),posix=os.name!="nt")
with tempfile.TemporaryDirectory(prefix="respect-mod-metadata-") as d:
 out=Path(d)/("test.exe" if os.name=="nt" else "test")
 subprocess.run([*cc,"-std=c11","-Wall","-Wextra","-Werror","-I"+str(r/"port/src"),"-I"+str(r/"port/include"),str(r/"tools/tests/modfs_memory_test.c"),str(r/"port/src/modfs_memory.c"),str(r/"port/src/modio.c"),"-o",str(out)],check=True)
 subprocess.run([str(out)],check=True)
