#!/usr/bin/env python3
"""Generate metadata for the verified PC NOISE layout from owned originals.

Usage: mod_noisenames.py <original PC noise/noise.bin> <USA disc.bin> <out.h>
The observed PC layout omits PS1 block15 and differs in block264. These are
explicit evidence-bound exceptions, not content-search guesses. Any additional
difference or trailing byte refuses generation. No game data is emitted.
"""
import argparse
from pathlib import Path
import struct
from mod_pcnames import disc_files, read_file
from pc_noise import build_profile, header


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pc", type=Path)
    parser.add_argument("disc", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    files = {n: (lba, size) for n, lba, size, _ in disc_files()}
    with args.disc.open("rb") as disc:
        kernel = read_file(disc, *files["KERNEL.BIN"])
        native = read_file(disc, *files["NOISE.BIN"])
    table = 0x80058D2C - struct.unpack_from("<I", kernel, 0x18)[0] + 0x800
    offsets = []
    for i in range(1024):
        pos = struct.unpack_from("<I", kernel, table + 4 * i)[0] * 2048
        offsets.append(pos)
        if pos == len(native):
            break
    profile = build_profile(native, args.pc.read_bytes(), offsets, {15}, {264})
    # The supported profile is deliberately specific to the verified layouts.
    if profile["pc_size"] != 16355328 or profile["native_size"] != 16625664:
        raise ValueError("this is not the verified PC/USA archive layout")
    with args.output.open("x", encoding="utf-8", newline="\n") as out:
        out.write(header(profile))
    print(f"{len(profile['parts'])} ordered metadata ranges; no assets written")


if __name__ == "__main__":
    main()
