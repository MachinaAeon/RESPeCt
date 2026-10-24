#!/usr/bin/env python3
"""For each overlay blob in a bundle, find where its code ends and check it
against a known load address.

Code end = the last `jr ra` before the first long run of non-instruction
words.  Then every absolute `jal` inside the code that targets the blob's
own range must land on a function start if the base is right.

Usage: overlay_code_extent.py <bundle.BIN> <base_hex>
"""
import struct, sys
sys.path.insert(0, __import__("os").path.dirname(__file__))
from find_overlay_base import words, blobs, is_func_start

def looks_like_insn(x):
    op = x >> 26
    if x == 0: return True                      # nop
    if op in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
              0x10, 0x12, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B,
              0x32, 0x3A): return True
    return False

def main():
    path, base = sys.argv[1], int(sys.argv[2], 16)
    d = open(path, 'rb').read(); w = words(d)
    for a, b in blobs(d, w):
        bw = w[a//4:b//4]
        # Code end = last `jr ra` inside the leading run of 256-byte chunks
        # that are mostly instruction-like.  Small embedded tables (jump
        # tables, constants) don't end the code; a long data run does.
        CH = 64                                 # words per chunk
        last_code_chunk = 0
        for c in range(0, len(bw), CH):
            chunk = bw[c:c+CH]
            frac = sum(looks_like_insn(x) for x in chunk) / len(chunk)
            if frac >= 0.80 and any(x == 0x03E00008 for x in chunk):
                last_code_chunk = c
            elif frac < 0.50 and c - last_code_chunk >= 4 * CH:
                break
        last_jr = max((i for i in range(last_code_chunk, min(len(bw), last_code_chunk + CH))
                       if bw[i] == 0x03E00008), default=0)
        code_end = (last_jr + 2) * 4            # include delay slot
        jals = [((x & 0x03FFFFFF) << 2) | 0x80000000 for x in bw[:code_end//4] if x >> 26 == 3]
        inside = [t for t in jals if base <= t < base + code_end]
        hits = sum(is_func_start(bw, (t - base)//4) for t in inside)
        outside_kernel = [t for t in jals if not (base <= t < base + code_end) and not (0x80010000 <= t < 0x80058000)]
        print(f"@{a:#07x} id={bw[0]:3d} code={code_end:#07x} of {b-a:#07x}  "
              f"own-jal hits {hits}/{len(inside)}  stray targets {len(outside_kernel)}")

if __name__ == "__main__":
    main()
