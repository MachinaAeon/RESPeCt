#!/usr/bin/env python3
"""Generate splat configs for the overlay bundles ENEMPROG.BIN and STAGPROG.BIN.

Each bundle is a run of overlays, each starting on a 2 KB sector with its
overlay ID (see tools/overlays.py).  Some follow the ID with a table of entry
points; others go straight into code.

Load addresses come from KERNEL's slot table at 0x80010040:
  stage slot    0x800F2C50   STAGPROG overlays 15-31
  enemy slot A  0x800F8000   ENEMPROG overlays 34-48
  enemy slot B  0x80100000   ENEMPROG overlays 50-64 (same enemies rebuilt
                              for the second slot, 0x8000 higher)
Each was cross-checked by every internal `jal` landing on a function start;
stage 22 is also confirmed from a RAM dump.  Enemy 40/56 score 8/15 in the
same pattern at their slots, so they're placed there but flagged.

STAGPROG overlay 32 has a different header (a file path, like TITLE.BIN) and
is kept as raw bytes until its load address is known.

Overlays in one slot share addresses, so names carry the overlay
(`func_800F81B0_ovl34`) and `exclusive_ram_id` keeps splat from resolving one
overlay's references into another.

Usage: gen_overlay_yaml.py   (run from the repo root; reads disc/)
"""
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from overlays import find_overlays, table_len, words  # noqa: E402

KERNEL_GP = 0x80062D1C      # $gp set up by KERNEL.BIN, shared by overlays
STAGE_SLOT = 0x800F2C50
ENEMY_SLOT_A = 0x800F8000
ENEMY_SLOT_B = 0x80100000

UNCONFIRMED = {40, 56}      # placed, but only 8/15 internal calls fit
RAW = {32}                  # different format; address unknown


def placement(bundle, ovl):
    """(base, exclusive_ram_id) for an overlay, or None to keep it raw."""
    if ovl in RAW:
        return None
    if bundle == "STAGPROG":
        return STAGE_SLOT, "stage_slot"
    if ovl < 50:
        return ENEMY_SLOT_A, "enemy_slot_a"
    return ENEMY_SLOT_B, "enemy_slot_b"


def code_end(d, a, b):
    """Offset (within the overlay) just past the last `jr ra` + delay slot."""
    w = words(d)
    last = max((i for i in range(a // 4, b // 4) if w[i] == 0x03E00008), default=None)
    return (last - a // 4 + 2) * 4 if last is not None else b - a


def gen(name):
    d = open(f"disc/{name}.BIN", "rb").read()
    low = name.lower()
    out = [
        f"name: {name}.BIN",
        f"sha1: {hashlib.sha1(d).hexdigest()}",
        "options:",
        f"  basename: {low}",
        f"  target_path: disc/{name}.BIN",
        f"  elf_path: build/{low}.elf",
        "  base_path: .",
        "  platform: psx",
        "  compiler: PSYQ",
        "  # Overlays reach KERNEL's globals through its $gp. Without this the",
        "  # linker invents a GOT and the output grows by 8 bytes.",
        f"  gp_value: 0x{KERNEL_GP:08X}",   # lowercase 0x: YAML reads 0X as text
        f"  ld_script_path: {low}.ld",
        f"  asm_path: asm/{low}",
        f"  asset_path: assets/{low}",
        "  symbol_addrs_path: [symbol_addrs.txt]",
        f"  undefined_funcs_auto_path: build/{low}_undefined_funcs_auto.txt",
        f"  undefined_syms_auto_path: build/{low}_undefined_syms_auto.txt",
        "  # Header (rodata) must come before code, as on disc.",
        '  section_order: [".rodata", ".text", ".data", ".bss"]',
        "  ld_align_section_vram_end: False",
        "  ld_align_segment_vram_end: False",
        "  subalign: 2",
        "  string_encoding: ASCII",
        "  data_string_encoding: ASCII",
        "  rodata_string_guesser_level: 2",
        "  data_string_guesser_level: 2",
        "segments:",
    ]
    for a, b, ovl, _ in find_overlays(d):
        seg = f"ovl{ovl:02d}"
        place = placement(name, ovl)
        if place is None:
            out += [f"  - name: {seg}", "    type: bin", f"    start: {a:#x}",
                    "    # different header format; load address not known yet"]
            continue
        base, ram_id = place
        hdr = (1 + table_len(d, a)) * 4
        end = code_end(d, a, b)
        note = "   # load address only partly confirmed (8/15 calls fit)" if ovl in UNCONFIRMED else ""
        out += [
            f"  - name: {seg}",
            "    type: code",
            f"    start: {a:#x}",
            f"    vram: {base:#x}{note}",
            f"    exclusive_ram_id: {ram_id}",
            '    symbol_name_format: "$VRAM_$SEG"',
            "    subsegments:",
            f"      - [{a:#x}, rodata, {seg}_header]   # overlay id + entry table",
            f"      - [{a + hdr:#x}, asm, {seg}]",
        ]
        if a + end < b:
            out.append(f"      - [{a + end:#x}, data, {seg}_data]")
    out.append(f"  - [{len(d):#x}]")
    open(f"{low}.yaml", "w").write("\n".join(out) + "\n")
    print(f"wrote {low}.yaml")


if __name__ == "__main__":
    for name in ("ENEMPROG", "STAGPROG"):
        gen(name)
