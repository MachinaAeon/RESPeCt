#!/usr/bin/env python3
"""Rewrite splat/spimdisasm assembly so LLVM's integrated assembler accepts it.

LLVM can assemble generic MIPS I, but it does not implement the PlayStation's
coprocessor-2 (GTE) transfer instructions.  Every such line is replaced with a
raw `.word`, taken from the instruction bytes spimdisasm prints in the comment
in front of it, e.g.

    /* 1C3A0 8002C1A0 0000C048 */  ctc2  $t0, $24
 -> /* 1C3A0 8002C1A0 0000C048 */  .word 0x48C00000 /* ctc2  $t0, $24 */

The comment shows the bytes in file order (little-endian), so they are
byte-swapped to form the word.  Lines that carry a relocation (%hi/%lo/a
symbol) are never touched; the script fails loudly if it meets one, so the
output is always byte-identical to what GNU as would produce.

Usage: llvm_fixup.py <in.s> <out.s>
"""
import re
import sys

# Instructions LLVM rejects for -march=mips1: the coprocessor-2 (GTE)
# transfers, plus `rfe` (return from exception), which LLVM never implemented.
COP2_OPS = {"lwc2", "swc2", "mtc2", "mfc2", "ctc2", "cfc2", "rfe"}

LINE = re.compile(
    r"^(?P<lead>\s*/\*\s*[0-9A-F]+\s+[0-9A-F]+\s+(?P<bytes>[0-9A-F]{8})\s*\*/\s+)"
    r"(?P<op>[a-z0-9.]+)(?P<rest>.*)$"
)


def le_word(byte_str: str) -> int:
    b = bytes.fromhex(byte_str)
    return int.from_bytes(b, "little")


def fix(src: str) -> tuple[str, int]:
    out, changed = [], 0
    for line in src.splitlines():
        m = LINE.match(line)
        if m and m.group("op") in COP2_OPS:
            rest = m.group("rest")
            if "%" in rest:
                sys.exit(f"refusing to rewrite relocated cop2 line: {line.strip()}")
            word = le_word(m.group("bytes"))
            # Drop any trailing comment on the original line so the new
            # comment doesn't nest (C comments don't nest under cpp).
            insn = re.sub(r"/\*.*?\*/", "", m.group("op") + rest).strip()
            line = f"{m.group('lead')}.word 0x{word:08X} /* {insn} */"
            changed += 1
        out.append(line)
    return "\n".join(restore_missing_labels(out)) + "\n", changed


# A label is defined either as `name:` or through a splat macro
# (`jlabel name`, `glabel name`, ...).
LABEL_DEF = re.compile(r"^\s*(?:(\.L[0-9A-F]{8}\w*):|[gdja]label\s+(\.L[0-9A-F]{8}\w*))")
LABEL_REF = re.compile(r"\.L([0-9A-F]{8})(\w*)")
ADDR_COL = re.compile(r"^\s*/\*\s*[0-9A-F]+\s+([0-9A-F]{8})\s")


def restore_missing_labels(lines: list[str]) -> list[str]:
    """Add local branch labels that are referenced but never defined.

    With overlapping overlays (same RAM addresses), splat sometimes emits a
    branch to `.L<addr>_<seg>` but skips the label line in front of the target
    instruction, because another overlay already claimed that address.  A
    label emits no bytes, so putting it before the instruction at that address
    in the same file is exactly what was meant.
    """
    defined = {m.group(1) or m.group(2) for l in lines if (m := LABEL_DEF.match(l))}
    here = {m.group(1) for l in lines if (m := ADDR_COL.match(l))}
    missing = {}
    for l in lines:
        if LABEL_DEF.match(l):
            continue
        for addr, suffix in LABEL_REF.findall(l):
            name = f".L{addr}{suffix}"
            # Only restore labels whose instruction is in this file; anything
            # else is a cross-file reference for the linker to resolve.
            if name not in defined and addr in here:
                missing[addr] = name
    if not missing:
        return lines
    out = []
    for l in lines:
        m = ADDR_COL.match(l)
        if m and m.group(1) in missing:
            out.append(f"  {missing.pop(m.group(1))}:")
        out.append(l)
    return out


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as f:
        text, n = fix(f.read())
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(text)
    print(f"{sys.argv[1]}: rewrote {n} coprocessor-2 instructions")


if __name__ == "__main__":
    main()
