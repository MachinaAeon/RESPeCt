"""Compare RAM snapshots of two runs (see run.sh): first frame that differs
outside the stacks, and where. Frames (PNGs) are compared too."""
import os, sys, glob
a, b = sys.argv[1], sys.argv[2]
IGNORE = [(0x1FD000, 0x200000)]          # stacks: main (801FFFA0) and threads (801FFA00, 801FF200, 801FEF00), growing down; enemy records sit lower in 801Fxxxx
def ignored(o): return any(lo <= o < hi for lo, hi in IGNORE)
frames = sorted(os.path.basename(p) for p in glob.glob(a + "/r*.bin"))
bad = None
for fr in frames:
    pa, pb = os.path.join(a, fr), os.path.join(b, fr)
    if not os.path.exists(pb): print("  missing", fr, "in new run (crash?)"); bad = fr; break
    x, y = open(pa, "rb").read(), open(pb, "rb").read()
    if x == y: continue
    diffs = [o for o in range(0, len(x), 4) if x[o:o+4] != y[o:o+4] and not ignored(o)]
    if not diffs: continue
    runs, start, prev = [], diffs[0], diffs[0]
    for o in diffs[1:]:
        if o - prev > 16: runs.append((start, prev)); start = o
        prev = o
    runs.append((start, prev))
    print(f"  {fr}: {len(diffs)} words differ in {len(runs)} ranges:",
          " ".join(f"{0x80000000+s:08X}-{0x80000000+e+3:08X}" for s, e in runs[:12]))
    bad = fr; break
pics = sorted(os.path.basename(p) for p in glob.glob(a + "/f*.png"))
pd = [p for p in pics if not os.path.exists(os.path.join(b, p)) or open(os.path.join(a, p), "rb").read() != open(os.path.join(b, p), "rb").read()]
for side in (a, b):
    log = open(os.path.join(side, "log.txt"), errors="replace").read()
    if "FATAL" in log: print("  FATAL in", side)
print(f"  {len(frames)} RAM snapshots, first difference: {bad or 'none'}; {len(pd)}/{len(pics)} screenshots differ" + (f" (first {pd[0]})" if pd else ""))
