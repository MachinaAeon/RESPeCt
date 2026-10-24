#!/usr/bin/env python3
"""Function tables for the interpreter build (port/meta/meta.c).

The release build of the port carries no code or data from the game: the
game's code is read from the player's disc and run by port/src/interp.c,
which follows exactly the same rules as the C that recomp.py writes (so
both builds behave identically, RAM for RAM). What that needs to know is
what recomp.py worked out from the code, written here as plain numbers:

  per module   its RAM range, and a hash of its first bytes (to tell which
               overlay is loaded; recomp.py's tables compare the bytes)
  per function where it starts, which addresses belong to it, how each `j`
               and `jr` was translated (a jump inside the function or a
               call; which jump table), the decompiled replacement if
               there is one, and the hook the port calls when it starts
  mod patches  the instruction words of tools/build_mod.py's replacements
               (compiled from the port's own C in src/mods), which the
               translated build runs instead of the disc's

Usage: meta.py            (writes port/meta/meta.c)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modules as M  # noqa: E402
import recomp as RC  # noqa: E402

OUT = os.path.join(M.ROOT, "port", "meta", "meta.c")
CONTROL = RC.BRANCHES | {"j", "jal", "jr", "jalr"}


def fnv64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def discover_all():
    """Same discovery as recomp.main()."""
    mods, _ = M.all_modules()
    kernel = mods[0]
    RC.M_KERNEL = kernel
    test = next(m for m in mods if m.name == "test")
    for _ in range(3):
        before = (len(kernel.seeds), len(test.seeds))
        for mod in mods:
            if mod is kernel:
                continue
            for f in RC.discover(mod).values():
                if f.bad:
                    continue
                for c in f.calls:
                    if kernel.has(c):
                        kernel.seeds.add(c)
                    elif mod.name.startswith("ovl") or mod.name == "option":
                        if test.has(c) and not mod.has(c):
                            test.seeds.add(c)
        if (len(kernel.seeds), len(test.seeds)) == before:
            break
    kernel_funcs = RC.discover(kernel)
    out = []
    for mod in mods:
        funcs = kernel_funcs if mod is kernel else RC.discover(mod)
        out.append((mod, {e: f for e, f in funcs.items() if not f.bad}))
    return out


def ranges(pcs):
    out = []
    for pc in sorted(pcs):
        if out and out[-1][1] == pc:
            out[-1][1] = pc + 4
        else:
            out.append([pc, pc + 4])
    return out


def annotations(mod, f, tables):
    """(pc, code) for every j and jr (not ra) the translation emits:
    0 jump inside the function, 1 call (tail call), 2 rt_call through a
    register, 3 + n jump table n (its address and size go in tables)."""
    delay = {pc + 4 for pc, i in f.insns.items() if i.kind in CONTROL}
    out = []
    for pc in sorted(f.insns):
        if pc in delay:
            continue
        i = f.insns[pc]
        if i.kind == "j":
            inside = i.target in f.labels and mod.has(i.target) and i.target in f.insns
            out.append((pc, 0 if inside else 1))
        elif i.kind == "jr" and i.rs != 31:
            if pc in f.tables:
                t = f.tables[pc]
                # the table is read from RAM at run time; find where it is
                lw = RC.find_def(mod, pc, i.rs, limit=16)
                add = RC.find_def(mod, lw.pc, lw.rs, limit=16)
                base = None
                for b, idx in ((add.rs, add.rt), (add.rt, add.rs)):
                    di = RC.find_def(mod, add.pc, idx, limit=16)
                    if di is not None and di.kind == "sll" and di.sa == 2:
                        v = RC.const_value(mod, add.pc, b)
                        if v is not None:
                            base = (v + lw.imm) & 0xFFFFFFFF
                            break
                assert base is not None and [mod.word(base + 4 * k) for k in range(len(t))] == t
                out.append((pc, 3 + len(tables)))
                tables.append((base, len(t)))
            else:
                out.append((pc, 2))
    return out


def patches(mod):
    """Runs of words where the modded KERNEL/TEST differ from the disc's."""
    if mod.name not in ("kernel", "test"):
        return []
    orig = M.read(f"disc/{mod.name.upper()}.BIN")
    if mod.name == "kernel":
        orig = orig[0x800:]
    img = mod.image
    out = []
    for o in range(0, min(len(img), len(orig)) - 3, 4):
        a, b = img[o:o + 4], orig[o:o + 4]
        if a == b:
            continue
        addr = mod.vram + o
        w = int.from_bytes(a, "little")
        if out and out[-1][0] + 4 * len(out[-1][1]) == addr:
            out[-1][1].append(w)
        else:
            out.append((addr, [w]))
    return out


def main():
    RC.NATIVE = RC.load_native()
    hooks = RC.load_hooks()
    found = discover_all()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    L = []
    w = L.append
    w("/* Generated by tools/recomp/meta.py: function tables for the interpreter")
    w(" * build. Addresses and hashes only; nothing from the game itself. */")
    w('#include "recomp.h"\n')
    natives = sorted({n.lstrip("~") for n in RC.NATIVE.values()})
    for n in natives:
        w(f"void n_{n}(void);")
    for h in sorted(set(hooks.values())):
        w(f"void {h}(void);")
    w("")
    stubs = []
    for mi, (mod, funcs) in enumerate(found):
        tables, rng, ann = [], [], []
        recs = []
        for e in sorted(funcs):
            f = funcs[e]
            r = ranges(f.insns)
            a = annotations(mod, f, tables)
            delay = {pc + 4 for pc, i in f.insns.items() if i.kind in CONTROL}
            flags = 1 if (delay & (f.labels | {e})) else 0      # jumps into a delay slot
            recs.append((e, len(rng), len(r), len(ann), len(a), flags, hooks.get((mod.name, e))))
            rng += r
            ann += a
        n = mod.name
        w(f"static const uint32_t rng_{n}[] = {{{', '.join(f'0x{x:08X}' for p in rng for x in p)}}};")
        w(f"static const uint32_t ann_{n}[] = {{{', '.join(f'0x{p:08X},{c}' for p, c in ann)}}};")
        w(f"static const uint32_t jt_{n}[] = {{{', '.join(f'0x{b:08X},{c}' for b, c in tables) or '0'}}};")
        w(f"static const InFunc in_{n}[] = {{")
        for e, ro, rn, ao, an, fl, hook in recs:
            w(f"    {{0x{e:08X}u, {ro}, {rn}, {ao}, {an}, {fl}, {hook or '0'}}},")
        w("};")
        w(f"static const RtFunc tab_{n}[] = {{")
        for k, (e, *_rest) in enumerate(recs):
            nat = RC.NATIVE.get((n, e))
            fn = f"n_{nat.lstrip('~')}" if nat else "0"
            w(f"    {{0x{e:08X}u, {fn}, 0, &in_{n}[{k}]}},")
            if nat and nat.startswith("~"):
                stubs.append((mi, k, f"f_{n}_{e:08X}"))
        w("};")
        p = patches(mod)
        words = [x for addr, ws in p for x in [addr, len(ws)] + ws]
        w(f"static const uint32_t patch_{n}[] = {{{', '.join(f'0x{x:08X}' for x in words) or '0'}}};")
        w("")
    w("const RtModule rt_modules[] = {")
    for mod, funcs in found:
        n = mod.name
        p = patches(mod)
        w(f"    {{\"{n}\", 0x{mod.vram:08X}u, 0x{mod.code[0]:08X}u, 0x{mod.code[1]:08X}u, 0, {len(mod.sig)}, "
          f"tab_{n}, {len(funcs)}, 0x{fnv64(mod.sig):016X}ull, rng_{n}, ann_{n}, jt_{n}, "
          f"{len(p) and 'patch_' + n or '0'}, {len(p)}}},")
    w("};")
    w(f"const int rt_module_count = {len(found)};\n")
    w("/* Translations the decompiled wrappers (\"~name\" in native.txt) call. */")
    for mi, k, name in stubs:
        w(f"void {name}(void) {{ interp_resume(&rt_modules[{mi}], &tab_{found[mi][0].name}[{k}]); }}")
    open(OUT, "w").write("\n".join(L) + "\n")
    total = sum(len(f) for _, f in found)
    print(f"{OUT}: {len(found)} modules, {total} functions, {len(natives)} natives")


if __name__ == "__main__":
    main()
