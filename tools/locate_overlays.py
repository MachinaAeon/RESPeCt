#!/usr/bin/env python3
"""Find which overlays are loaded in a PS1 RAM dump, and where.

For every overlay blob in ENEMPROG.BIN and STAGPROG.BIN, take 64-byte
samples of its code and look for them in a 2 MB RAM dump (taken in
DuckStation).  A sample found at RAM offset R from blob offset O means the
blob's start sits at 0x80000000 + R - O.  Several samples agreeing on the
same start = that overlay is loaded there.

Code that patches itself or relocated data would disagree, so the script
reports how many samples agree rather than trusting one hit.

Usage: locate_overlays.py <ram_dump.bin> [disc_dir]
"""
import os
import struct
import sys
from collections import Counter

SAMPLE = 64          # bytes per sample
STEP = 0x400         # sample every 1 KB of each blob


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from overlays import find_overlays


def blobs(d):
    return [(a, b, i) for a, b, i, _ in find_overlays(d)]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ram = open(sys.argv[1], "rb").read()
    disc = sys.argv[2] if len(sys.argv) > 2 else "disc"
    if len(ram) not in (0x200000, 0x800000):
        print(f"warning: dump is {len(ram):#x} bytes, expected 2 MB")
    for name in ("STAGPROG", "ENEMPROG"):
        d = open(os.path.join(disc, f"{name}.BIN"), "rb").read()
        print(f"== {name}")
        for a, b, ovl in blobs(d):
            starts, tried = Counter(), 0
            for off in range(a + 0x40, b - SAMPLE, STEP):
                chunk = d[off:off + SAMPLE]
                if chunk.count(0) > SAMPLE // 2:
                    continue             # skip mostly-empty samples
                tried += 1
                pos = ram.find(chunk)
                while pos != -1:
                    starts[0x80000000 + pos - (off - a)] += 1
                    pos = ram.find(chunk, pos + 1)
            # Overlays share some small routines, so a few stray hits are
            # normal. Call it loaded only when most samples agree.
            start, n = starts.most_common(1)[0] if starts else (0, 0)
            if tried and n / tried >= 0.5:
                print(f"  ovl{ovl:02d}  LOADED at {start:#010x}  "
                      f"({n}/{tried} samples agree)")
            elif n >= 3:
                print(f"  ovl{ovl:02d}  partial match at {start:#010x}  "
                      f"({n}/{tried}) - shared code, or only part loaded")
            elif "-v" in sys.argv:
                print(f"  ovl{ovl:02d}  not loaded")


if __name__ == "__main__":
    main()
