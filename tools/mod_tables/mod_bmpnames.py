#!/usr/bin/env python3
"""Which of the PS1 disc's 2D pictures (TIMs) each of the PC version's BMPs is.

The PC version (gun_bio) has its 2D pictures as BMP files (PSX_DATA/map,
PSX_DATA/RESULT, Resource...), drawn from the same pictures as the PS1's
TIMs and, for these, at the same size. This pairs each BMP with the TIM
it shows (same size, nearly the same picture) and writes
port/src/mods/tables/modfs_bmp.h, so a mod can give the PC's BMP and RESPeCt puts it
in the TIM's place (port/src/mods/modfs.c), in the TIM's own format.

Usage: mod_bmpnames.py <PC version folder (PSX_DATA, Resource)> <disc .cue/.bin> [out.h]
Needs numpy. BMPs the PS1 builds otherwise (text drawn from font sheets,
pictures at another size) aren't listed.
"""
import os
import re
import struct
import sys
import zlib

import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
BPP = {0: 4, 1: 8, 2: 16}


def disc_files():
    src = open(os.path.join(here, '..', '..', 'port', 'src', 'disc_ids.h')).read()
    return [(n, int(l), int(s), int(h, 16)) for n, l, s, h in re.findall(r'\{"([^"]+)", (\d+)u, (\d+)u, 0x([0-9A-F]+)ull\}', src)]


def read_file(binf, lba, size):
    out = bytearray()
    for s in range((size + 2047) // 2048):
        binf.seek((lba + s) * 2352 + 24)
        out += binf.read(2048)
    return bytes(out[:size])


def rgb15(v):
    r, g, b = (v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3
    return np.stack([r | r >> 5, g | g >> 5, b | b >> 5], -1).astype(np.float32)


def tims(d):
    """(offset, length, w, h, gray picture) of each TIM of 64x64 or more in d."""
    i = 0
    while True:
        i = d.find(b'\x10\x00\x00\x00', i)
        if i < 0 or i + 20 > len(d):
            return
        flag = struct.unpack_from('<I', d, i + 4)[0]
        j, clut = i + 8, None
        try:
            if flag & ~0xB or (flag & 3) not in BPP:
                raise ValueError
            if flag & 8:
                cl, cx, cy, cw, ch = struct.unpack_from('<IHHHH', d, j)
                if cl != 12 + cw * ch * 2 or not cw or not ch:
                    raise ValueError
                clut = np.frombuffer(d, '<u2', cw, j + 12)
                j += cl
            il, ix, iy, iw, ih = struct.unpack_from('<IHHHH', d, j)
            if il != 12 + iw * ih * 2 or not iw or not ih or ix >= 1024 or iy >= 512 or j + il > len(d):
                raise ValueError
        except (ValueError, struct.error):
            i += 4
            continue
        bpp = BPP[flag & 3]
        w = iw * 16 // bpp
        raw = d[j + 12:j + il]
        if w >= 64 and ih >= 64 and (bpp == 16 or clut is not None):
            if bpp == 16:
                img = rgb15(np.frombuffer(raw, '<u2').reshape(ih, iw))
            elif bpp == 8:
                img = rgb15(clut[np.frombuffer(raw, np.uint8).reshape(ih, w)])
            else:
                b = np.frombuffer(raw, np.uint8).reshape(ih, iw * 2)
                img = rgb15(clut[np.stack([b & 15, b >> 4], -1).reshape(ih, w)])
            yield i, j + il - i, w, ih, img.mean(-1)
        i = j + il


def bmp(path):
    """A BMP file's picture (gray, top row first), or None."""
    d = open(path, 'rb').read()
    if d[:2] != b'BM':
        return None
    off, = struct.unpack_from('<I', d, 10)
    hs, w, h, planes, bits, comp = struct.unpack_from('<IiiHHI', d, 14)
    if comp not in (0, 3) or bits not in (4, 8, 16, 24, 32):
        return None
    flip, h = h > 0, abs(h)
    stride = (w * bits + 31) // 32 * 4
    rows = np.frombuffer(d, np.uint8, stride * h, off).reshape(h, stride)
    if bits <= 8:
        n = struct.unpack_from('<I', d, 46)[0] or (1 << bits)
        pal = np.frombuffer(d, np.uint8, n * 4, 14 + hs).reshape(n, 4)[:, 2::-1].astype(np.float32)
        if bits == 8:
            idx = rows[:, :w]
        else:
            idx = np.stack([rows >> 4, rows & 15], -1).reshape(h, stride * 2)[:, :w]
        img = pal[np.minimum(idx, n - 1)]
    elif bits == 16:
        v = rows[:, :w * 2].copy().view('<u2')
        img = np.stack([(v >> 10) & 31, (v >> 5) & 31, v & 31], -1).astype(np.float32) * 8
    else:
        img = rows[:, :w * bits // 8].reshape(h, w, bits // 8)[:, :, 2::-1].astype(np.float32)
    if flip:
        img = img[::-1]
    return img.mean(-1)


def corr(a, b):
    a, b = a - a.mean(), b - b.mean()
    return float((a * b).sum() / (np.sqrt((a * a).sum() * (b * b).sum()) + 1e-6))


def main():
    pc, disc = sys.argv[1], sys.argv[2]
    out_h = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, '..', '..', 'port', 'src', 'mods', 'tables', 'modfs_bmp.h')
    if disc.lower().endswith('.cue'):
        disc = os.path.join(os.path.dirname(disc), re.search(r'FILE "([^"]+)"', open(disc).read()).group(1))
    binf = open(disc, 'rb')
    pics = []                                    # (disc file, offset, length, w, h, picture, crc)
    for name, lba, size, code in disc_files():
        if code or name.endswith('.STR') or size < 4096:
            continue
        d = read_file(binf, lba, size)
        for off, n, w, h, img in tims(d):
            pics.append((name, off, n, w, h, img, zlib.crc32(d[off:off + n]) & 0xFFFFFFFF))
    rows = []
    for top in ('PSX_DATA', 'Resource'):
        for root, _, files in os.walk(os.path.join(pc, top)):
            for f in sorted(files):
                if not f.upper().endswith('.BMP'):
                    continue
                p = os.path.join(root, f)
                img = bmp(p)
                if img is None:
                    continue
                best = max(((corr(img, t[5]), t) for t in pics if t[3] == img.shape[1] and t[4] == img.shape[0]),
                           default=None, key=lambda c: c[0])
                if best and best[0] > 0.9:
                    rel = os.path.relpath(p, os.path.join(pc, 'PSX_DATA') if top == 'PSX_DATA' else pc)
                    rows.append((rel.replace('\\', '/').upper(), best[1]))
    rows.sort()
    with open(out_h, 'w', newline='\n') as o:
        o.write('/* The PC version\'s BMPs that are the PS1 disc\'s TIMs (same size): PC path (PSX_DATA\'s,\n'
                ' * or RESOURCE/...), disc file, the TIM\'s offset in it, its length and CRC-32.\n'
                ' * Made by tools/mod_tables/mod_bmpnames.py. */\n')
        o.write('static const struct { const char *pc, *disc; uint32_t offset, size, crc; } modfs_bmp[] = {\n')
        for rel, t in rows:
            o.write('    {"%s", "%s", %du, %du, 0x%08Xu},\n' % (rel, t[0], t[1], t[2], t[6]))
        o.write('};\n')
    print('%d BMPs paired with a TIM -> %s' % (len(rows), out_h))


if __name__ == '__main__':
    main()
