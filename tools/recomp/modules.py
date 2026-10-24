"""Code modules of Resident Evil Survivor for the static recompiler.

A module is one piece of code as it sits in RAM: its bytes, load address,
and the function starts we already know. Overlays that share a RAM slot are
separate modules; at run time the one actually loaded is picked by
comparing its first bytes (its signature) with RAM.

Function starts come from the splat split (asm/*.s glabels) where a module
has been split, and from entry points (exe entry, slot-0 load table,
overlay entry tables). The recompiler finds the rest by following calls.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from overlays import find_overlays  # noqa: E402

SLOT0 = 0x8006C118
STAGE_SLOT = 0x800F2C50
ENEMY_A = 0x800F8000
ENEMY_B = 0x80100000
SLOT4 = 0x800C3E48

# KERNEL code: file 0x1300 - 0x48EC8 (rodata before, data after).
KERNEL_CODE = (0x80010000 + 0x1300 - 0x800, 0x80010000 + 0x48EC8 - 0x800)


class Module:
    def __init__(self, name, image, vram, code=None, seeds=(), sig_len=32):
        self.name = name
        self.image = image                  # bytes as loaded in RAM
        self.vram = vram
        self.end = vram + len(image)
        # Range that may hold code (calls into it go through this module).
        self.code = code or (vram, self.end)
        self.seeds = set(seeds)
        self.sig = image[:sig_len]          # compared against RAM at run time

    def word(self, addr):
        return struct.unpack_from("<I", self.image, addr - self.vram)[0]

    def has(self, addr):
        return self.code[0] <= addr < self.code[1]


def read(path):
    return open(os.path.join(ROOT, path), "rb").read()


GLABEL = re.compile(r"^glabel (\w+)")
INSN = re.compile(r"/\* [0-9A-F]+ ([0-9A-F]{8}) [0-9A-F]{8} \*/")


def splat_functions(path):
    """{vram: name} for every function glabel in a splat .s file."""
    out, pending = {}, None
    full = os.path.join(ROOT, path)
    if not os.path.exists(full):
        return out
    for line in open(full):
        m = GLABEL.match(line)
        if m:
            pending = m.group(1)
            continue
        if pending:
            m = INSN.search(line)
            if m:
                out[int(m.group(1), 16)] = pending
                pending = None
            elif line.startswith(("dlabel", "glabel", "endlabel")) or ".word" in line:
                pending = None
    return out


def ptr_table(image, vram, start):
    """Code pointers right after an overlay's ID word (or at start + 4)."""
    out, o = [], start + 4 if start == 0 else start
    while o + 4 <= len(image):
        x = struct.unpack_from("<I", image, o)[0]
        if not (vram <= x < vram + len(image)):
            break
        out.append(x)
        o += 4
    return out


def slot0_entries():
    """{overlay name: [entry points]} from KERNEL's slot-0 load table
    (0x80059618: 12-byte entries of disc position pointer, sector count,
    entry point). Entries are matched to files by disc position."""
    k = read("disc/KERNEL.BIN")
    o = 0x80059618 - 0x80010000 + 0x800
    by_pos = {0x8005852C: "debug", 0x80058514: "test", 0x80058518: "title",
              0x80058520: "typew", 0x80058524: "score", 0x80058528: "staff"}
    out = {}
    for i in range(9):
        pos, count, entry = struct.unpack_from("<III", k, o + 12 * i)
        out.setdefault(by_pos[pos], []).append(entry)
    return out


def all_modules():
    names = {}
    mods = []

    # The PC port uses the modded KERNEL/TEST (FPS controls, built by
    # tools/build_mod.py) when present; RECOMP_ORIGINAL=1 uses the disc's.
    use_mods = not os.environ.get("RECOMP_ORIGINAL") and os.path.exists(os.path.join(ROOT, "build/mods/TEST_mod.BIN"))

    def code_file(name):
        if use_mods and name in ("KERNEL", "TEST"):
            return read(f"build/mods/{name}_mod.BIN")
        return read(f"disc/{name}.BIN")

    k = code_file("KERNEL")[0x800:]
    f = splat_functions("asm/1300.s")
    names.update(f)
    mods.append(Module("kernel", k, 0x80010000, code=KERNEL_CODE,
                       seeds=set(f) | {0x80010B08}))

    entries = slot0_entries()
    for name in ("TEST", "TITLE", "TYPEW", "SCORE", "STAFF", "OPTION", "DEBUG"):
        img = code_file(name)
        f = splat_functions(f"asm/{name.lower()}/{name.lower()}.s")
        names.update({a: n for a, n in f.items()})
        seeds = set(f) | set(entries.get(name.lower(), []))
        seeds |= set(ptr_table(img, SLOT0, 0))
        base = SLOT0
        if name == "OPTION":
            # The in-game options menu: loaded at slot 4, right after TEST
            # (it calls TEST's functions). Its internal calls fit only there.
            base = SLOT4
            seeds |= {SLOT4 + 0x38}
        mods.append(Module(name.lower(), img, base, seeds=seeds))

    for bundle, path in (("ENEMPROG", "enemprog"), ("STAGPROG", "stagprog")):
        d = read(f"disc/{bundle}.BIN")
        for start, end, oid, _ in find_overlays(d):
            if bundle == "STAGPROG":
                base = STAGE_SLOT           # (stage 17's, ovl32, starts with a file name, like TITLE)
            else:
                base = ENEMY_A if oid < 49 else ENEMY_B
            img = d[start:end]
            f = splat_functions(f"asm/{path}/ovl{oid}.s")
            seeds = set(f) | set(ptr_table(img, base, 0x1C if img[4:6] == b".." else 0))
            mods.append(Module(f"ovl{oid}", img, base, seeds=seeds, sig_len=64))
    # Code the game unpacks at run time (captured from RAM).
    for line in open(os.path.join(ROOT, "tools/recomp/captured.txt")):
        line = line.split("#")[0].split()
        if len(line) < 2:
            continue
        path = os.path.join(ROOT, "disc", "captured", f"{line[0]}.bin")
        if not os.path.exists(path):
            print(f"note: {path} not captured yet", file=sys.stderr)
            continue
        img = open(path, "rb").read()
        base = int(line[1], 16)
        seeds = set(ptr_table(img, base, 0x1C if img[4:6] == b".." else 0))
        mods.append(Module(line[0], img, base, seeds=seeds))

    by_name = {m.name: m for m in mods}
    lines = list(open(os.path.join(ROOT, "tools/recomp/extra_entries.txt")))
    learned = os.path.join(ROOT, "tools/recomp/learned_entries.txt")
    if os.path.exists(learned):
        lines += list(open(learned))
    for line in lines:
        line = line.split("#")[0].split()
        if len(line) == 2:
            by_name[line[0]].seeds.add(int(line[1], 16))
    return mods, names


if __name__ == "__main__":
    mods, names = all_modules()
    for m in mods:
        print(f"{m.name:8s} {m.vram:#010x}-{m.end:#010x} seeds={len(m.seeds)}")
