#!/usr/bin/env python3
"""Guess the RAM load address of each overlay blob in a bundle file.

A blob starts on a 2 KB sector with: word0 = overlay ID, then a table of
code pointers into RAM.  For each candidate base address, score how many
table pointers land on a plausible function start (a stack-frame setup
`addiu sp,sp,-N`, or right after a `jr ra; nop` return).  Prints the best
base per blob with its score.

Usage: find_overlay_base.py <bundle.BIN>
"""
import struct, sys

def words(d): return struct.unpack_from(f'<{len(d)//4}I', d)

def blobs(d, w):
    heads = [o for o in range(0, len(d), 0x800)
             if w[o//4] < 0x100 and 0x800E0000 <= w[o//4+1] < 0x80100000]
    return list(zip(heads, heads[1:] + [len(d)]))

def is_func_start(bw, i):
    if not 0 <= i < len(bw): return False
    x = bw[i]
    if x >> 16 == 0x27BD and x & 0x8000: return True          # addiu sp,sp,-N
    if i >= 2 and bw[i-2] == 0x03E00008: return True          # after jr ra + delay slot
    return False

def main():
    d = open(sys.argv[1], 'rb').read(); w = words(d)
    for a, b in blobs(d, w):
        bw = w[a//4:b//4]
        run = 0
        while 0x80000000 <= bw[1+run] < 0x80200000: run += 1
        ptrs = sorted(set(bw[1:1+run]))
        best = None
        hi = min(ptrs)
        for base in range(hi - (b - a) + 4, hi + 4, 4):
            offs = [(p - base)//4 for p in ptrs]
            if any(o < 1 + run for o in offs): continue  # can't point into header
            score = sum(is_func_start(bw, o) for o in offs)
            if best is None or score > best[0]: best = (score, base)
        # Stronger signal: absolute `jal` targets inside the blob must land on
        # function starts at the true base.  Try every 4-byte base that keeps
        # the table pointers inside the blob and score by jal hits.
        jals = [((x & 0x03FFFFFF) << 2) | 0x80000000 for x in bw if x >> 26 == 3]
        best2 = None
        for base in range(max(ptrs) - (b - a) + 4, min(ptrs) + 4, 4):
            inside = [t for t in jals if base <= t < base + (b - a)]
            if not inside: continue
            hits = sum(is_func_start(bw, (t - base)//4) for t in inside)
            key = (hits, len(inside))
            if best2 is None or key > best2[0]: best2 = (key, base, len(inside))
        if best2:
            (hits, n), base2, _ = best2
            print(f"@{a:#07x} id={bw[0]:3d} jal-base={base2:#010x} "
                  f"end={base2 + (b - a):#010x} jal hits={hits}/{n} "
                  f"(table-base {best[1]:#010x})")
        else:
            print(f"@{a:#07x} id={bw[0]:3d} no internal jal targets found")

if __name__ == "__main__":
    main()
