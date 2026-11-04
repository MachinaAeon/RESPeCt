#!/usr/bin/env python3
"""Which room sound set's sequence each of the PC version's BGM WAVs is.

The PC version (gun_bio) plays its music from WAVs (PSX_DATA/BGM/NNN-BGM.WAV,
NNN-BGM-WITH-M.WAV when the room has monsters): recordings of the PS1's
sequences. The bench plays every sequence of every sound set in the
headless runner (the plugin tools/plugins/seqrec.c, its start frames in the
log) and records it (--wav); this compares each PC WAV with each recording
(log energy in 24 bands, 50 ms frames, normalised correlation over lags of
+-3 s) and keeps a match only when it is clear:
  - a score of 0.5 or more, 0.15 above the next sequence's; or
  - a WITH-M/plain pair of one PC room: when one of them matches set S's
    sequence 2 (or 1) clearly, the other takes S's other sequence if that
    is among its three best with a score of 0.35 or more.
It writes port/src/modfs_pc_bgm.h: PC name -> NOISE.BIN block (0xE6 + set)
and music number, which a mod can then give by the PC's name.

Usage: mod_bgmnames.py <BGM folder> <recording.wav> <runner log> [out.h]
Needs numpy.
"""
import glob, os, re, sys, wave
import numpy as np

SR = 4000


def load(p, start=0.0, secs=12.0):
    w = wave.open(p)
    r, n, c = w.getframerate(), w.getnframes(), w.getnchannels()
    a0 = min(int(start * r), n)
    w.setpos(a0)
    a = np.frombuffer(w.readframes(min(int(secs * r), n - a0)), '<i2').astype(np.float32).reshape(-1, c).mean(1)
    return np.interp(np.arange(0, len(a) / r, 1 / SR), np.arange(len(a)) / r, a)


def feat(a):
    hop, win = SR // 20, np.hanning(512)
    fr = [np.abs(np.fft.rfft(a[i:i + 512] * win)) for i in range(0, len(a) - 512, hop)]
    if len(fr) < 100:
        return None
    S = np.array(fr)
    e = np.geomspace(4, 255, 25).astype(int)
    B = np.log(np.stack([S[:, e[k]:e[k + 1] + 1].sum(1) for k in range(24)], 1) + 1e-3)
    return (B - B.mean(0)) / (B.std(0) + 1e-6)


def score(x, y):
    best, n = -1.0, min(len(x), len(y)) - 60
    for lag in range(-60, 61, 2):
        a, b = x[max(0, lag):max(0, lag) + n], y[max(0, -lag):max(0, -lag) + n]
        m = min(len(a), len(b))
        if m >= 80:
            best = max(best, float((a[:m] * b[:m]).mean()))
    return best


def main():
    bgm, rec, log = sys.argv[1:4]
    out = sys.argv[4] if len(sys.argv) > 4 else os.path.join(os.path.dirname(__file__), '..', 'port', 'src', 'modfs_pc_bgm.h')
    text = open(log).read()
    f0 = int(re.search(r'savestate: loaded .*?, frame (\d+)', text).group(1))
    ps1 = {}
    for f, s, m in re.findall(r'\[\s*(\d+)\] plugin: seqrec: set (\d+) music(\d) from', text):
        F = feat(load(rec, (int(f) - f0) / 59.94 + 0.3, 13))
        if F is not None:
            ps1[(int(s), int(m))] = F
    ranked = {}
    for p in sorted(glob.glob(os.path.join(bgm, '*'))):
        if not p.lower().endswith('.wav'):
            continue
        F = feat(load(p, 0, 13))
        if F is not None:
            ranked[os.path.basename(p).upper()] = sorted(((score(G, F), k) for k, G in ps1.items()), reverse=True)
    take = {}
    for n, r in ranked.items():
        if r[0][0] >= 0.5 and r[0][0] - r[1][0] >= 0.15:
            take[n] = r[0][1]
    for n, r in ranked.items():                  # the other half of a pair
        if n in take:
            continue
        m = re.match(r'(\d+)-BGM(-WITH-M)?', n)
        if not m:
            continue
        for o, k in list(take.items()):
            mo = re.match(r'(\d+)-BGM(-WITH-M)?', o)
            if mo and mo.group(1) == m.group(1) and bool(mo.group(2)) != bool(m.group(2)):
                want = (k[0], 1 if m.group(2) is None else 2)
                if any(kk == want and sc >= 0.35 for sc, kk in r[:3]):
                    take[n] = want
    with open(out, 'w', newline='\n') as o:
        o.write('/* The PC version\'s BGM WAVs that are recordings of a room sound set\'s sequence: PC name\n'
                ' * (PSX_DATA\'s), NOISE.BIN block (0xE6 + set), music number. Made by tools/mod_bgmnames.py\n'
                ' * from recordings of every sequence (tools/plugins/seqrec.c). */\n')
        o.write('static const struct { const char *pc; int block, music; } modfs_pc_bgm[] = {\n')
        for n in sorted(take):
            s, mus = take[n]
            o.write('    {"BGM/%s", %d, %d},   /* score %.2f */\n' % (n, 0xE6 + s, mus, dict((kk, sc) for sc, kk in ranked[n])[take[n]]))
        o.write('};\n')
    print('%d of %d PC music WAVs matched -> %s' % (len(take), len(ranked), out))
    for n in sorted(take):
        print('  ', n, '-> set %d music%d' % take[n])


if __name__ == '__main__':
    main()
