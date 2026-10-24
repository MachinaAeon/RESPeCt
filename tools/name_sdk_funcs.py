#!/usr/bin/env python3
"""Name Psy-Q SDK library functions in the split KERNEL.BIN assembly.

Sony's libraries print debug messages that start with the function's own
name (e.g. "DrawSync(%d)...").  A function that loads the address of one of
those strings is therefore that library function.  This needs no copy of the
SDK: it only uses strings already present in the game binary.

Prints `name = 0xADDR; // type:func` lines for splat's symbol_addrs.txt and
reports any string that matched no function, or more than one.

Usage: name_sdk_funcs.py <rodata.s> <code.s> [more code.s ...]
"""
import re
import sys
from collections import defaultdict

# Debug-string prefix -> SDK function that prints it.
KNOWN = {
    "ResetGraph:jtb=": "ResetGraph",
    "ResetGraph(%d)...": "ResetGraph",
    "SetGraphDebug:level": "SetGraphDebug",
    "SetGrapQue(%d)...": "SetGraphQue",
    "DrawSyncCallback(%08x)...": "DrawSyncCallback",
    "SetDispMask(%d)...": "SetDispMask",
    "DrawSync(%d)...": "DrawSync",
    "ClearOTag(%08x,%d)...": "ClearOTag",
    "ClearOTagR(%08x,%d)...": "ClearOTagR",
    "DrawOTag(%08x)...": "DrawOTag",
    "PutDrawEnv(%08x)...": "PutDrawEnv",
    "DrawOTagEnv(%08x,": "DrawOTagEnv",
    "PutDispEnv(%08x)...": "PutDispEnv",
    "CdInit: Init failed": "CdInit",
    # libgpu passes its own name to the shared RECT checker.
    "ClearImage": "ClearImage",
    "ClearImage2": "ClearImage2",
    "LoadImage": "LoadImage",
    "LoadImage2": "LoadImage2",
    "StoreImage": "StoreImage",
    "MoveImage": "MoveImage",
}

STR_LINE = re.compile(r'/\*\s*[0-9A-F]+\s+([0-9A-F]{8})\s*\*/\s+\.asciz\s+"(.*)"')
FUNC = re.compile(r"^glabel\s+(\w+)")
REF = re.compile(r"%lo\((D_[0-9A-F]{8})\)")


def strings(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        m = STR_LINE.search(line)
        if m:
            out[f"D_{m.group(1)}"] = m.group(2).encode().decode("unicode_escape")
    return out


def refs_by_func(paths):
    refs = defaultdict(set)
    for path in paths:
        cur = None
        for line in open(path, encoding="utf-8"):
            m = FUNC.match(line)
            if m:
                cur = m.group(1)
                continue
            if cur:
                for sym in REF.findall(line):
                    refs[cur].add(sym)
    return refs


def match(text):
    t = text.rstrip("\n")
    exact = [n for p, n in KNOWN.items() if t == p]
    if exact:
        return exact[0]
    # Longest prefix wins so "ResetGraph(%d)..." doesn't hit "ResetGraph".
    pre = sorted((p for p in KNOWN if len(p) > 12 and t.startswith(p)), key=len)
    return KNOWN[pre[-1]] if pre else None


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    strs = strings(sys.argv[1])
    refs = refs_by_func(sys.argv[2:])
    found = defaultdict(set)
    for func, syms in refs.items():
        for sym in syms:
            if sym in strs:
                name = match(strs[sym])
                if name:
                    found[name].add(func)
    for name in sorted(set(KNOWN.values())):
        funcs = found.get(name, set())
        if len(funcs) == 1:
            addr = next(iter(funcs)).split("_")[-1]
            print(f"{name} = 0x{addr}; // type:func")
        elif not funcs:
            print(f"// {name}: no function references its string", file=sys.stderr)
        else:
            print(f"// {name}: ambiguous {sorted(funcs)}", file=sys.stderr)


if __name__ == "__main__":
    main()
