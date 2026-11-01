#!/usr/bin/env python3
"""Which of the PS1 disc's streamed sound channels each of the PC version's WAVs is.

The PC version (gun_bio) has the voices and the streamed music as WAV
files (PSX_DATA/wav/Scene05_1.wav...): each one a channel of one of the
PS1 disc's XA streams, decoded, at the same rate (some cut short). This pairs
each with its channel and writes port/src/modfs_wav.h, so a mod can give
the PC's WAV by its PC name (port/src/modfs.c plays it in the channel's
place).

Usage: mod_wavnames.py <PSX_DATA folder> <disc .cue/.bin> [out.h]
Needs numpy.
"""
import os
import re
import sys
import wave

import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from xa_dump import decode, disc_files   # noqa: E402


def main():
    pc, disc = sys.argv[1], sys.argv[2]
    out_h = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, '..', 'port', 'src', 'modfs_wav.h')
    if disc.lower().endswith('.cue'):
        disc = os.path.join(os.path.dirname(disc), re.search(r'FILE "([^"]+)"', open(disc).read()).group(1))
    f = open(disc, 'rb')
    chans = []                                   # (stream, channel, frames, left)
    for name, lba, size in disc_files():
        if not name.endswith('.STR'):
            continue
        cs = {}
        for k in range((size + 2047) // 2048):
            f.seek((lba + k) * 2352)
            sec = f.read(2352)
            if len(sec) < 2352 or sec[15] != 2 or not sec[18] & 0x04 or sec[17] >= 32:
                continue
            c = cs.setdefault(sec[17], {'hist': [[0, 0], [0, 0]], 'l': []})
            (l, r), stereo, rate = decode(sec, c['hist'])
            c['l'] += l
        for ch, c in cs.items():
            chans.append((name, ch, len(c['l']), np.array(c['l'][:30000], np.float32)))
    rows = []
    for root, _, files in os.walk(pc):
        for fn in sorted(files):
            if not fn.lower().endswith('.wav'):
                continue
            p = os.path.join(root, fn)
            try:
                w = wave.open(p)
            except Exception:
                continue
            n, nc = w.getnframes(), w.getnchannels()
            if w.getsampwidth() != 2 or not n:
                continue
            a = np.frombuffer(w.readframes(min(n, 30000)), '<i2').astype(np.float32)
            a = a.reshape(-1, nc)[:, 0] if nc > 1 else a
            best = None
            for name, ch, frames, left in chans:
                if n > frames:                   # (the PC cut some short: the silence at the end)
                    continue
                m = min(len(a), len(left))
                if m < 64:
                    continue
                x, y = a[:m] - a[:m].mean(), left[:m] - left[:m].mean()
                c = float((x * y).sum() / (np.sqrt((x * x).sum() * (y * y).sum()) + 1e-9))
                if c > 0.99 and (best is None or (c, frames == n) > best[0]):
                    best = ((c, frames == n), name, ch)
            if best:
                rel = os.path.relpath(p, pc).replace(os.sep, '/').upper()
                rows.append((rel, best[1], best[2]))
    rows.sort()
    with open(out_h, 'w', newline='\n') as o:
        o.write('/* The PC version\'s WAVs that are channels of the PS1 disc\'s XA streams: PC path (PSX_DATA\'s),\n'
                ' * stream, channel. Made by tools/mod_wavnames.py. */\n')
        o.write('static const struct { const char *pc, *stream; int chan; } modfs_wav[] = {\n')
        for r in rows:
            o.write('    {"%s", "%s", %d},\n' % r)
        o.write('};\n')
    print('%d WAVs paired with a stream channel -> %s' % (len(rows), out_h))


if __name__ == '__main__':
    main()
