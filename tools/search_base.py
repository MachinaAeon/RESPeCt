#!/usr/bin/env python3
"""Rank candidate load addresses for one overlay blob by internal-call fit.

Usage: search_base.py <bundle.BIN> <blob_offset_hex> [lo_hex hi_hex]
Prints the top candidates: hits/total of absolute `jal` targets that fall in
the blob's range and land on a function start.
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from find_overlay_base import words, blobs, is_func_start

d = open(sys.argv[1], 'rb').read(); w = words(d)
off = int(sys.argv[2], 16)
lo = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x80058000
hi = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x80100000
a, b = next(x for x in blobs(d, w) if x[0] == off)
bw = w[a//4:b//4]
jals = [((x & 0x03FFFFFF) << 2) | 0x80000000 for x in bw if x >> 26 == 3]
res = []
for base in range(lo, hi, 4):
    inside = [t for t in jals if base <= t < base + (b - a)]
    if len(inside) < 5: continue
    hits = sum(is_func_start(bw, (t - base)//4) for t in inside)
    res.append((hits / len(inside), hits, len(inside), base))
res.sort(reverse=True)
for r, h, n, base in res[:3]:
    print(f"  base={base:#010x}  {h}/{n} ({r:.0%})")
