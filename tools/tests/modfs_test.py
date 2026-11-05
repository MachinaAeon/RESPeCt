"""Asset-free regression tests against the production mod loader.
Run with Python 3; optionally set CC to a clang/GCC executable.
"""
from pathlib import Path
import os, shlex, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
cc=shlex.split(os.environ.get("CC", "clang"), posix=os.name!="nt")
with tempfile.TemporaryDirectory(prefix="respect-modfs-") as out:
    exe=Path(out)/("modfs_test.exe" if os.name=="nt" else "modfs_test")
    subprocess.run([*cc,"-O1","-ffunction-sections","-fdata-sections","-Wl,--gc-sections",
        *["-I"+str(root/p) for p in ["port/include","port/src","port/third_party"]],
        str(Path(__file__).with_suffix(".c")),str(root/"port/src/modfs_memory.c"),str(root/"port/src/modio.c"),"-o",str(exe),"-lm"],check=True)
    failed=[]
    for scenario in ['sync-tim-font-off','sync-tim-font-on','sync-tim-font-custom-off','sync-tim-font-malformed','sync-tim-font-no-space','sync-tim-image-on','sync-tim-image-off','scenery-part','scenery-first','scenery-edge','scenery-whole','scenery-whole-custom','scenery-stage5','scenery-stage5-edge','scenery-stage6','scenery-stage6-edge','scenery-outside',"split-staging-on","split-staging-off","split-staging-malformed","split-staging-excluded","split-staging-raw","split-staging-ev","oversized","overflow","allocation","valid","whole","chars","hd","hd-zero","diary","hd-state","long-name","ui","lz-invalid","list","identity-order","hd-palettes","packed-growth","hd-font-banks","hd-transparency","hd-black","hd-duplicate","raw-models","pc-package","scripts-parse","pc-audio-route","pc-audio-families","pc-noise-identical","pc-noise-change","pc-noise-invalid","pc-noise-readonly","pc-noise-budget","pc-noise-size","pc-noise-overlap","pc-noise-reference-variant","pc-noise-reserved","pc-noise-shift","pc-noise-version","pc-noise-header-size","wav-format-bounds","wav-truncated","wav-empty","wav-loop-truncated","wav-rate-range"]:
        print(scenario,flush=True)
        r=subprocess.run([str(exe),scenario],cwd=out)
        if r.returncode: failed.append((scenario,r.returncode))
    if failed: raise SystemExit("FAILED: "+str(failed))
