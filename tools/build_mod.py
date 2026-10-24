#!/usr/bin/env python3
"""Compile C replacements for game functions and splice them in place.

Targets:
  kernel  src/mods/kernel/<function>.c  replaces functions in KERNEL.BIN
  test    src/mods/<function>.c         replaces functions in TEST.BIN

Each file defines a function named after an existing one in that target
(e.g. pad_GetButtons, player_AimMode). For each one:

  1. compile with clang for the PS1 CPU (MIPS I, o32, no PIC, no $gp)
  2. link at the original function's address, resolving every game symbol
     (functions and globals) to its real address
  3. check the entry lands exactly on the original address and the code +
     read-only data fit in the original function's bytes
  4. copy it over the original in a copy of the file (rest padded with nops)

A file may also define extra functions that other mods call (a helper
placed in a slot with spare room); providers are linked first. KERNEL is
built before TEST so TEST mods can call KERNEL helpers.

Output: build/mods/KERNEL_mod.BIN and build/mods/TEST_mod.BIN, which
tools/make_patches.py turns into DuckStation memory patches. The disc
files are never modified.

Usage: build_mod.py              build every mod in every target
       build_mod.py kernel|test  build one target
"""
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build/mods")

TARGETS = {
    # name: (asm with the original functions, disc file, RAM->file offset, mod sources)
    "kernel": ("asm/1300.s", "disc/KERNEL.BIN", lambda a: a - 0x80010000 + 0x800,
               "src/mods/kernel"),
    "test": ("asm/test/test.s", "disc/TEST.BIN", lambda a: a - 0x8006C118,
             "src/mods"),
}
ORDER = ["kernel", "test"]

CFLAGS = ["--target=mipsel-unknown-none-elf", "-march=mips1", "-mabi=32",
          "-mno-abicalls", "-fno-pic", "-G0", "-Oz", "-ffreestanding",
          "-fno-builtin", "-msoft-float", "-fomit-frame-pointer",
          # one section per function, so the linker can put the replaced
          # function first, at the original entry address
          "-ffunction-sections",
          "-fno-jump-tables", "-mno-check-zero-division", "-Wall", "-Wno-unused-command-line-argument",
          "-I", os.path.join(ROOT, "include/c")]


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"failed: {' '.join(cmd[:3])} ...")
    return r


def function_table(asm_path):
    """name -> (address, size) for every function in a split .s file."""
    sizes, addrs, pending = {}, {}, None
    for line in open(os.path.join(ROOT, asm_path), encoding="utf-8"):
        m = re.match(r"^nonmatching (\S+), (0x[0-9A-F]+)", line)
        if m:
            sizes[m.group(1)] = int(m.group(2), 16)
            continue
        m = re.match(r"^glabel (\S+)", line)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r"^\s*/\*\s*[0-9A-F]+\s+([0-9A-F]{8})", line)
        if m and pending:
            addrs[pending] = int(m.group(1), 16)   # first instruction
            pending = None
    return {k: (addrs[k], sizes[k]) for k in sizes if k in addrs}


def game_symbols():
    """Every named symbol we know from symbol_addrs.txt."""
    syms = {}
    for line in open(os.path.join(ROOT, "symbol_addrs.txt"), encoding="utf-8"):
        m = re.match(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+);", line)
        if m:
            syms[m.group(1)] = int(m.group(2), 16)
    return syms


def link(name, obj, addr, syms, elf):
    script = os.path.join(OUT, f"{name}.ld")
    undef_script = os.path.join(OUT, f"{name}.syms.ld")
    with open(script, "w") as f:
        f.write(f"""SECTIONS {{
  /* SUBALIGN(4): clang gives the (empty) plain .text 16-byte alignment,
     which would push the start past the original entry address. MIPS
     code only needs 4. */
  .text 0x{addr:08X} : SUBALIGN(4) {{ *(.text.{name}) *(.text*) *(.rodata*) }}
  /DISCARD/ : {{ *(.MIPS.*) *(.reginfo) *(.pdr) *(.comment) *(.gnu.*)
                *(.mdebug*) *(.note*) *(.llvm_addrsig) *(.got) }}
}}
""")

    def try_link(extra):
        # --error-limit=0: we read the undefined names from this output, and
        # lld otherwise stops listing them after 20.
        return subprocess.run(["ld.lld", "-m", "elf32ltsmip", "-e", "0", "--error-limit=0",
                               "-T", script, *extra, "-o", elf, obj],
                              capture_output=True, text=True)
    r = try_link([])
    if r.returncode:
        names = sorted(set(re.findall(r"undefined symbol: (\S+)", r.stderr)))
        defs = []
        for n in names:
            m = re.match(r"^(?:func|D|jtbl)_([0-9A-F]{8})(?:_\w+)?$", n)
            if n in syms:
                defs.append(f"{n} = 0x{syms[n]:08X};")
            elif m:
                defs.append(f"{n} = 0x{m.group(1)};")
            else:
                raise SystemExit(f"{name}: unknown symbol {n} (add it to symbol_addrs.txt)")
        open(undef_script, "w").write("\n".join(defs) + "\n")
        r = try_link(["-T", undef_script])
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit(f"{name}: link failed")


def check_sections(name, obj):
    r = run(["llvm-readelf", "-S", obj])
    for sec in (".data", ".bss", ".sdata", ".sbss"):
        m = re.search(rf"\s{re.escape(sec)}\s+\S+\s+\S+\s+\S+\s+([0-9a-f]+)", r.stdout)
        if m and int(m.group(1), 16):
            raise SystemExit(f"{name}: has writable {sec}; replacements may only use the game's globals")


def nm_symbols(path, kinds):
    """Global symbol names of the given nm kinds (e.g. 'T' defined, 'U' undefined)."""
    out = set()
    for line in run(["llvm-nm", path]).stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] in kinds:
            out.add(parts[-1])
    return out


def build_order(src_dir, mods):
    """Compile every mod, then order them so a mod defining a function
    another one calls (a helper placed in a roomier slot) is linked first."""
    defines, needs = {}, {}
    for name in mods:
        obj = os.path.join(OUT, f"{name}.o")
        run(["clang", *CFLAGS, "-c", os.path.join(ROOT, src_dir, f"{name}.c"), "-o", obj])
        check_sections(name, obj)
        defines[name] = nm_symbols(obj, {"T"})
        needs[name] = nm_symbols(obj, {"U"})
    provider = {sym: m for m, syms in defines.items() for sym in syms}
    order, state = [], {}

    def visit(m):
        if state.get(m) == "done":
            return
        if state.get(m) == "active":
            raise SystemExit(f"{m}: helpers call each other in a cycle")
        state[m] = "active"
        for sym in sorted(needs[m]):
            p = provider.get(sym)
            if p and p != m:
                visit(p)
        state[m] = "done"
        order.append(m)

    for m in mods:
        visit(m)
    return order


def build_target(target, syms):
    asm_path, disc, file_off, src_dir = TARGETS[target]
    funcs = function_table(asm_path)
    mods = [os.path.splitext(os.path.basename(p))[0]
            for p in sorted(glob.glob(os.path.join(ROOT, src_dir, "*.c")))]
    image = bytearray(open(os.path.join(ROOT, disc), "rb").read())
    for name in build_order(src_dir, mods):
        if name not in funcs:
            raise SystemExit(f"{name}: not a {target} function")
        addr, size = funcs[name]
        obj = os.path.join(OUT, f"{name}.o")
        elf = os.path.join(OUT, f"{name}.elf")
        binf = os.path.join(OUT, f"{name}.bin")
        link(name, obj, addr, syms, elf)
        # Functions this mod exports can now be called by later mods.
        for line in run(["llvm-nm", "--defined-only", elf]).stdout.splitlines():
            a, kind, sym = line.split()
            if kind == "T":
                syms[sym] = int(a, 16)
        # The game calls the original entry address, so the replacement must
        # start exactly there (not a static helper placed ahead of it).
        nm = run(["llvm-nm", elf]).stdout
        entry = next((int(l.split()[0], 16) for l in nm.splitlines()
                      if l.split()[-1] == name), None)
        if entry != addr:
            raise SystemExit(f"{name}: linked entry {entry and hex(entry)} != original {addr:#x}")
        run(["llvm-objcopy", "-O", "binary", "--only-section=.text", elf, binf])
        code = open(binf, "rb").read()
        if len(code) > size:
            raise SystemExit(f"{name}: C version is {len(code):#x} bytes, original slot is {size:#x}")
        off = file_off(addr)
        image[off:off + size] = code + bytes(size - len(code))
        print(f"{target}: {name}: {len(code):#x} of {size:#x} bytes at {addr:#010x}")
    out = os.path.join(OUT, os.path.basename(disc).replace(".BIN", "_mod.BIN"))
    open(out, "wb").write(image)


def main():
    os.makedirs(OUT, exist_ok=True)
    targets = sys.argv[1:] or ORDER
    for t in targets:
        if t not in TARGETS:
            raise SystemExit(__doc__)
    syms = game_symbols()
    for t in ORDER:
        if t in targets:
            build_target(t, syms)


if __name__ == "__main__":
    main()
