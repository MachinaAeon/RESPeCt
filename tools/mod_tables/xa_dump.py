#!/usr/bin/env python3
"""Write the disc's streamed sound (XA: music, voices, movies' sound) as WAV
files named the way a mod replaces them (docs/MODDING.md):
<out>/X/BGM_32.STR/05.wav is channel 5 of X/BGM_32.STR, at its own rate.

Usage: xa_dump.py <disc .cue/.bin> <out folder> [stream ...]
With no streams named, every .STR file on the disc. Edit a WAV (keep its
length to stay in step with the game: what's past the channel's end is
cut, what's missing is silence) and put it in mods/<your mod>/ at the
same path.
"""
import os
import re
import struct
import sys
import wave

here = os.path.dirname(os.path.abspath(__file__))
POS = [0, 60, 115, 98, 122]
NEG = [0, 0, -52, -55, -60]


def disc_files():
    src = open(os.path.join(here, '..', '..', 'port', 'src', 'disc_ids.h')).read()
    return [(n, int(l), int(s)) for n, l, s in re.findall(r'\{"([^"]+)", (\d+)u, (\d+)u', src)]


def decode(sec, hist):
    """One XA sector (2352 bytes) to [left], [right] (right empty when mono): spu.c's spu_xa_sector."""
    coding = sec[19]
    stereo = (coding & 3) == 1
    bits8 = ((coding >> 4) & 3) == 1
    out = ([], [])
    for g in range(18):
        grp = sec[24 + g * 128:24 + g * 128 + 128]
        for u in range(4 if bits8 else 8):
            hdr = grp[4 + u]
            shift, filt = hdr & 0xF, (hdr >> 4) & 3
            if shift > 12:
                shift = 9
            ch = (u & 1) if stereo else 0
            h = hist[ch]
            for i in range(28):
                if bits8:
                    t = struct.unpack('b', bytes([grp[16 + i * 4 + u]]))[0] << 8
                else:
                    n = (grp[16 + i * 4 + (u >> 1)] >> ((u & 1) * 4)) & 0xF
                    t = (n - 16 if n & 8 else n) << 12
                s = (t >> shift) + ((h[0] * POS[filt] + h[1] * NEG[filt] + 32) >> 6)
                s = max(-32768, min(32767, s))
                h[1], h[0] = h[0], s
                out[ch].append(s)
    return out, stereo, 18900 if (coding >> 2) & 1 else 37800


def main():
    disc, out = sys.argv[1], sys.argv[2]
    want = set(a.upper() for a in sys.argv[3:])
    if disc.lower().endswith('.cue'):
        disc = os.path.join(os.path.dirname(disc), re.search(r'FILE "([^"]+)"', open(disc).read()).group(1))
    f = open(disc, 'rb')
    for name, lba, size in disc_files():
        if not name.endswith('.STR') or (want and name.upper() not in want):
            continue
        chans = {}
        for k in range((size + 2047) // 2048):
            f.seek((lba + k) * 2352)
            sec = f.read(2352)
            if len(sec) < 2352 or sec[15] != 2 or not sec[18] & 0x04:
                continue                              # not sound
            c = chans.setdefault(sec[17], {'hist': [[0, 0], [0, 0]], 'l': [], 'r': [], 'stereo': False, 'rate': 0})
            (l, r), stereo, rate = decode(sec, c['hist'])
            c['l'] += l
            c['r'] += r if stereo else l
            c['stereo'], c['rate'] = stereo, rate
        for ch, c in sorted(chans.items()):
            d = os.path.join(out, *name.split('/'))
            os.makedirs(d, exist_ok=True)
            p = os.path.join(d, '%02d.wav' % ch)
            w = wave.open(p, 'wb')
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(c['rate'])
            w.writeframes(b''.join(struct.pack('<hh', a, b) for a, b in zip(c['l'], c['r'])))
            w.close()
            print('%s channel %d: %.1f s at %d Hz -> %s' % (name, ch, len(c['l']) / c['rate'], c['rate'], p))


if __name__ == '__main__':
    main()
