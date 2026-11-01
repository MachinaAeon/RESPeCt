#!/usr/bin/env python3
"""Where the bosses' textures (enemy types 31-33) are on the PS1 disc.

The PC version's ENEM31-33.NAB have each texture as a picture; on the
PS1 they are parts of 8-bit TIMs (OBJ.DAT, EV_OBJ.DAT), drawn with one
of the TIM's palette rows, often in several copies (one for each stage
the boss is in). This finds, for each of the NAB's textures, every TIM
it is a part of, and writes port/src/modfs_boss.h, so a mod's NAB puts
its textures there (port/src/modfs.c). Some of the PC's are redrawn and
not on the disc; those aren't listed.

Usage: mod_bossnames.py <the PC version's nab folder> <disc .cue/.bin> [out.h]
Needs numpy.
"""
import os
import re
import struct
import sys
import zlib

import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from mod_packnames import disc_files, read_file, unpack   # noqa: E402

ARCHIVES = {'OBJ.DAT': 0x800A0CD8, 'EV_OBJ.DAT': 0x800A195C}
TEST_BASE = 0x8006C118


def nab_textures(b):
    """(material, texture, VRAM x, y, used w, h, 15-bit pixels) of each NAB texture."""
    nv = struct.unpack_from('<I', b, 0)[0]
    at = 8 + nv * 36
    nm = struct.unpack_from('<I', b, at)[0]
    at += 4
    out = []
    for k in range(nm):
        w = struct.unpack_from('<88I', b, at)
        hw = struct.unpack('<176H', b[at:at + 352])
        nt = w[2]
        at += 352
        for t in range(nt):
            size, _, W, H = struct.unpack_from('<4I', b, at)
            px = np.frombuffer(b, '<u2', W * H, at + 16).reshape(H, W)
            at += 16 + size
            if nt > 1:
                vx, vy, uw, uh = hw[6 + t], hw[26 + t], w[48 + t], w[68 + t]
            else:
                vx, vy, uw, uh = w[3], ((w[1] >> 4) & 1) * 256, W, H
            conv = ((px >> 10) & 31) | ((px >> 5) & 31) << 5 | (px & 31) << 10
            out.append((k, t, vx, vy, uw, uh, conv[:uh, :uw]))
    return out


def tims8(d):
    """8-bit TIMs with a palette: (offset, length, VRAM x, y, w, h, palette rows, indices)."""
    i = 0
    while True:
        i = d.find(b'\x10\x00\x00\x00\x09\x00\x00\x00', i)
        if i < 0 or i + 40 > len(d):
            return
        try:
            cl, cx, cy, cw, ch = struct.unpack_from('<IHHHH', d, i + 8)
            if cl != 12 + cw * ch * 2:
                raise ValueError
            j = i + 8 + cl
            il, ix, iy, iw, ih = struct.unpack_from('<IHHHH', d, j)
            if il != 12 + iw * ih * 2 or j + il > len(d):
                raise ValueError
        except (ValueError, struct.error):
            i += 4
            continue
        clut = np.frombuffer(d, '<u2', cw * ch, i + 20).reshape(ch, cw)
        idx = np.frombuffer(d, np.uint8, iw * 2 * ih, j + 12).reshape(ih, iw * 2)
        yield i, j + il - i, ix, iy, iw * 2, ih, clut, idx
        i = j + il


def main():
    nab, disc = sys.argv[1], sys.argv[2]
    out_h = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, '..', 'port', 'src', 'modfs_boss.h')
    if disc.lower().endswith('.cue'):
        disc = os.path.join(os.path.dirname(disc), re.search(r'FILE "([^"]+)"', open(disc).read()).group(1))
    binf = open(disc, 'rb')
    files = disc_files()
    test = read_file(binf, *files['TEST.BIN'])
    blocks = []                                  # raw blocks: (disc file, block, data)
    for name, tab in ARCHIVES.items():
        d = read_file(binf, *files[name])
        nsec = (len(d) + 2047) // 2048
        t = [0]
        while t[-1] < nsec:
            t.append(struct.unpack_from('<I', test, tab - TEST_BASE + 4 * len(t))[0])
        for b in range(len(t) - 1):
            if t[b + 1] > t[b]:
                blocks.append((name, b, d[t[b] * 2048:t[b + 1] * 2048]))
    rows = []
    for typ in (31, 32, 33):
        b = open(os.path.join(nab, 'ENEM%02d.NAB' % typ), 'rb').read()
        for k, t, vx, vy, uw, uh, conv in nab_textures(b):
            for name, blk, d in blocks:
                for off, ln, ix, iy, w, h, clut, idx in tims8(d):
                    ox, oy = (vx - ix) * 2, vy - iy
                    if ox < 0 or oy < 0 or ox + uw > w or oy + uh > h:
                        continue
                    sub = idx[oy:oy + uh, ox:ox + uw]
                    for r in range(clut.shape[0]):
                        if np.mean((clut[r][sub] & 0x7FFF) == conv) > 0.99:
                            rows.append([typ, k, t, name, blk, off, r, ox, oy, uw, uh, zlib.crc32(d[off:off + ln]) & 0xFFFFFFFF, ln])
                            break
    # a palette row only one texture is drawn with can be made again for it
    for r in rows:
        r.append(int(sum(1 for q in rows if q[3:7] == r[3:7]) == 1))
    with open(out_h, 'w', newline='\n') as o:
        o.write('/* Where the bosses\' textures (the PC\'s ENEM31-33.NAB) are on the PS1 disc: enemy type,\n'
                ' * the NAB\'s material and texture, disc file, block, the TIM\'s offset in it, the palette\n'
                ' * row it\'s drawn with, its place in the TIM (x, y, w, h), the TIM\'s CRC-32 and length,\n'
                ' * and whether that row is only its own. Made by tools/mod_bossnames.py. */\n')
        o.write('static const struct { int type, mat, tex; const char *disc; uint32_t block, offset, row, x, y, w, h, crc, len; int own; } modfs_boss[] = {\n')
        for r in rows:
            o.write('    {%d, %d, %d, "%s", %du, %du, %du, %du, %du, %du, %du, 0x%08Xu, %du, %d},\n' % tuple(r))
        o.write('};\n')
    print('%d places for %d textures -> %s' % (len(rows), len(set(tuple(r[:3]) for r in rows)), out_h))


if __name__ == '__main__':
    main()
