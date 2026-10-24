#!/usr/bin/env python3
"""Save code the game unpacked at run time from a RAM dump of the port.

The image runs from <start> to the end of its code: the last `jr ra`
before 128 bytes of zeros (or to <end> if given).

Usage: capture.py <ram_dump.bin> <name> <start_hex> [end_hex]
Then add the name and address to tools/recomp/captured.txt.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    dump, name, start = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    d = open(dump, "rb").read()
    w = lambda a: struct.unpack_from("<I", d, a & 0x1FFFFF)[0]
    if len(sys.argv) > 4:
        end = int(sys.argv[4], 16)
    else:
        a, last = start, start
        while a < 0x80200000 - 128:
            if w(a) == 0x03E00008:
                last = a
            if all(w(a + 4 * k) == 0 for k in range(32)) and a > last:
                break
            a += 4
        end = a
    out = os.path.join(ROOT, "disc", "captured", f"{name}.bin")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(d[start & 0x1FFFFF:end & 0x1FFFFF])
    print(f"{name}: {start:#010x}-{end:#010x} ({end - start} bytes) -> {out}")


if __name__ == "__main__":
    main()
