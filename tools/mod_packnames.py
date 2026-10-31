#!/usr/bin/env python3
"""Where the PC version's files sit inside the PS1 disc's packed archives.

OBJ.DAT, EV_OBJ.DAT and ITEMDAT.DAT are archives of blocks (TEST.BIN has
their block tables) and most blocks are packed with the game's LZSS:
either whole, or as a header (3, then where each part starts) and three
packed parts (an enemy's textures, animations and model). The PC version
keeps some of what's in them as loose files: the items' and Vincent's
models and textures (PSX_DATA/item, people), and in its Resource folder
textures and models of enemies and bosses. This finds each one, unpacked,
and writes port/src/modfs_pack.h, so a mod can give the PC's file and
RESPeCt puts it in, packing the block again (port/src/modfs.c).

Usage: mod_packnames.py <PC version folder (PSX_DATA, Resource)> <disc .cue/.bin> [out.h]
A file found in several places (a texture two enemies share) is listed
for each; files the disc has loose (mod_pcnames.py) aren't.
"""
import os
import re
import struct
import sys
import zlib

here = os.path.dirname(os.path.abspath(__file__))
ARCHIVES = {'OBJ.DAT': 0x800A0CD8, 'EV_OBJ.DAT': 0x800A195C, 'ITEMDAT.DAT': 0x800A3EDC}   # block tables in TEST.BIN
TEST_BASE = 0x8006C118


def disc_files():
    src = open(os.path.join(here, '..', 'port', 'src', 'disc_ids.h')).read()
    return {n: (int(l), int(s)) for n, l, s in re.findall(r'\{"([^"]+)", (\d+)u, (\d+)u', src)}


def read_file(binf, lba, size):
    out = bytearray()
    for s in range((size + 2047) // 2048):
        binf.seek((lba + s) * 2352 + 24)
        out += binf.read(2048)
    return bytes(out[:size])


def unpack(d, s=0):
    """The game's LZSS (port/decomp/kernel_files.c), or None."""
    if s + 4 > len(d):
        return None
    size = struct.unpack_from('<I', d, s)[0]
    if not 16 <= size < 4 << 20:
        return None
    s += 4
    out, flags = bytearray(), 0
    try:
        while len(out) < size:
            if not flags & 0x100:
                flags = d[s] | 0xFF00
                s += 1
            if flags & 1:
                out.append(d[s])
                s += 1
            else:
                w = d[s] | d[s + 1] << 8
                s += 2
                dist, n = w >> 5, (w & 31) + 3
                if not dist:
                    return None
                for _ in range(n):
                    out.append(out[-dist] if len(out) >= dist else 0)
            flags >>= 1
    except IndexError:
        return None
    return bytes(out[:size])


def main():
    pc, disc = sys.argv[1], sys.argv[2]
    out_h = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, '..', 'port', 'src', 'modfs_pack.h')
    if disc.lower().endswith('.cue'):
        disc = os.path.join(os.path.dirname(disc), re.search(r'FILE "([^"]+)"', open(disc).read()).group(1))
    binf = open(disc, 'rb')
    files = disc_files()
    test = read_file(binf, *files['TEST.BIN'])
    places = []                                  # (disc file, block, part, data); part -2 the block as it is, -1 unpacked whole
    for name, tab in ARCHIVES.items():
        d = read_file(binf, *files[name])
        nsec = (len(d) + 2047) // 2048
        t = [0]
        while t[-1] < nsec:
            t.append(struct.unpack_from('<I', test, tab - TEST_BASE + 4 * len(t))[0])
        for b in range(len(t) - 1):
            blk = d[t[b] * 2048:t[b + 1] * 2048]
            if not blk:
                continue
            places.append((name, b, -2, blk))
            if struct.unpack_from('<2I', blk, 0) == (3, 16):
                offs = struct.unpack_from('<3I', blk, 4)
                for k, o in enumerate(offs):
                    u = unpack(blk, o)
                    if u:
                        places.append((name, b, k, u))
            else:
                u = unpack(blk, 0)
                if u:
                    places.append((name, b, -1, u))
    rows = []
    for top in ('PSX_DATA', 'Resource'):
        for root, _, names in os.walk(os.path.join(pc, top)):
            for fn in sorted(names):
                p = os.path.join(root, fn)
                data = open(p, 'rb').read()
                if len(data) < 64 or len(data) > 4 << 20:
                    continue
                hits = []
                for name, b, k, u in places:
                    i = u.find(data)
                    if i >= 0 and u.find(data, i + 1) < 0:
                        hits.append((name, b, k, i))
                raw = [h for h in hits if h[2] == -2]
                if not hits or (raw and len(raw) == len(hits) == 1):
                    continue                     # not there, or loose on the disc (mod_pcnames.py)
                if raw and len(raw) < len(hits):
                    hits = [h for h in hits if h[2] != -2]    # packed somewhere: the unpacked place
                rel = os.path.relpath(p, os.path.join(pc, 'PSX_DATA') if top == 'PSX_DATA' else pc)
                for h in hits:
                    rows.append((rel.replace('\\', '/').upper(),) + h + (len(data), zlib.crc32(data) & 0xFFFFFFFF))
    rows.sort()
    with open(out_h, 'w', newline='\n') as o:
        o.write('/* The PC version\'s files found inside the PS1 disc\'s packed archives: PC path\n'
                ' * (PSX_DATA\'s, or RESOURCE/...), disc file, block, part (-2 the block as it is,\n'
                ' * -1 the block unpacked whole, 0-2 its packed part), offset, size, CRC-32.\n'
                ' * Made by tools/mod_packnames.py. */\n')
        o.write('static const struct { const char *pc, *disc; uint32_t block; int32_t part; uint32_t offset, size, crc; } modfs_pack[] = {\n')
        for r in rows:
            o.write('    {"%s", "%s", %du, %d, %du, %du, 0x%08Xu},\n' % r)
        o.write('};\n')
    print('%d places for %d PC files -> %s' % (len(rows), len(set(r[0] for r in rows)), out_h))


if __name__ == '__main__':
    main()
