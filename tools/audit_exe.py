#!/usr/bin/env python3
"""Check a release build carries nothing from the game: every 32-byte run
of the exe (at each 4-byte step) is looked up among the 32-byte runs of the
disc's files (each 4-byte step), skipping runs that are nearly constant.

Usage: audit_exe.py EXE [disc image]"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W = 32


def interesting(b):
    return len(set(b)) >= 8


def main():
    exe = open(sys.argv[1], "rb").read()
    disc = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "disc/image/survivor.bin")
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    # file contents from the image (user data of every sector, file by file)
    f = open(disc, "rb")
    exe_runs = {}
    for o in range(0, len(exe) - W + 1, 4):
        b = exe[o:o + W]
        if interesting(b):
            exe_runs.setdefault(b, o)
    print(f"exe: {len(exe)} bytes, {len(exe_runs)} distinct runs")
    f.seek(0, 2)
    n = f.tell() // 2352
    f.seek(0)
    hits = 0
    prev = b""
    for lba in range(n):
        s = f.read(2352)
        data = s[24:24 + 2048] if (s[18] & 0x20) == 0 else s[24:24 + 2324]
        buf = prev + data
        for o in range(0, len(buf) - W + 1, 4):
            b = buf[o:o + W]
            if b in exe_runs:
                hits += 1
                if hits <= 20:
                    print(f"  match: disc sector {lba} +{o - len(prev)} = exe +{exe_runs[b]:#x}: {b.hex()}")
        prev = data[-(W - 4):]
    print(f"{hits} matching runs")


if __name__ == "__main__":
    main()
