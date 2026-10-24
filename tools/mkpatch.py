#!/usr/bin/env python3
"""Turn a modified slot-0 overlay (TEST.BIN, TITLE.BIN, ...) into a DuckStation memory patch.

TEST.BIN is loaded whole at 0x8006C118, so any change to the file is a
change to fixed RAM addresses.  This diffs a modified copy against the
original at 16-bit granularity and writes GameShark codes:

    D006C118 0006      only if TEST.BIN is the overlay in slot 0 (its first
                       word is 6; TITLE is 7, OPTION 12, DEBUG 5, ...)
    80AAAAAA VVVV      write 16 bits

Each write gets its own guard because a GameShark D0 code applies only to
the line after it.  DuckStation applies the section every frame
(Activation = EndFrame), so if TEST.BIN is reloaded the patch is restored
on the next frame.

Usage:
  mkpatch.py <original TEST.BIN> <modified TEST.BIN> <name> <description> [out.cht]
"""
import sys

BASE = 0x8006C118          # slot-0 load address (TEST, TITLE, TYPEW, ...)
GUARD_ADDR = BASE          # first word of the file: unique per slot-0 overlay
                           # (TEST 6, TITLE 7, TYPEW 8, SCORE 9, ...); taken
                           # from the original file, so patches only apply
                           # while that file is the one loaded


def diff_halfwords(a: bytes, b: bytes):
    if len(a) != len(b):
        sys.exit(f"size differs ({len(a)} vs {len(b)}); patches can only change bytes in place")
    for off in range(0, len(a) - 1, 2):
        if a[off:off + 2] != b[off:off + 2]:
            if off < 4:
                sys.exit("refusing to patch the guard word itself")
            yield BASE + off, int.from_bytes(b[off:off + 2], "little")


def section(name, desc, writes, guard_value):
    lines = [f"[{name}]", "Type = Gameshark", "Activation = EndFrame",
             "Author = Resident Evil Survivor PC port project",
             f"Description = {desc}", ""]
    for addr, val in writes:
        lines.append(f"D0{GUARD_ADDR & 0xFFFFFF:06X} {guard_value:04X}")
        lines.append(f"80{addr & 0xFFFFFF:06X} {val:04X}")
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) not in (5, 6):
        sys.exit(__doc__)
    orig = open(sys.argv[1], "rb").read()
    mod = open(sys.argv[2], "rb").read()
    writes = list(diff_halfwords(orig, mod))
    if not writes:
        sys.exit("no differences")
    guard_value = int.from_bytes(orig[0:2], "little")
    text = section(sys.argv[3], sys.argv[4], writes, guard_value)
    if len(sys.argv) == 6:
        with open(sys.argv[5], "a", encoding="ascii") as f:
            f.write(text + "\n")
    else:
        sys.stdout.write(text)
    print(f"{len(writes)} halfword write(s)", file=sys.stderr)


if __name__ == "__main__":
    main()
