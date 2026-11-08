#!/usr/bin/env python3
"""Which room sound set's sequence each of the PC version's BGM WAVs is.

The PC version (gun_bio) plays its music from WAVs (PSX_DATA/BGM/NNN-BGM.WAV,
NNN-BGM-WITH-M.WAV when the room has monsters): recordings of the PS1's
sequences. The bench plays every sequence of every sound set in the
headless runner (the plugin tools/plugins/seqrec.c, 45 s each, its start
frames in the log) and records it (--wav). This cuts each PC WAV into 5 s
pieces and looks for each piece in each recording (the waveforms'
correlation, normalised by the recording's energy under the piece, at
4 kHz): a sequence's score is its best piece's. A match is kept only when
it is clear:
  - a score of 0.35 or more, 0.15 above the next sequence's; or
  - the other half of a PC room's pair (NNN-BGM/NNN-BGM-WITH-M,
    NNN-BGM1/NNN-BGM2): when one of them matches set S's sequence 1 (or 2)
    clearly, the other takes S's other sequence if that scores 0.35 or more
    and is among its three best.
Sequences whose bytes are the same (the extract's NOISE/<block>/music<n>.seq)
are one: a PC WAV that is one of them is given for each.
Two PC rooms' WAVs can be the same PS1 sequence (017-BGM and 044-BGM): the
one that matches best is that sequence's, the other is marked alt and is
used only when a mod doesn't have the first one.
It writes port/src/mods/tables/modfs_pc_bgm.h: PC name -> NOISE.BIN block (0xE6 + set),
music number and alt, which a mod can then give by the PC's name.

Usage: mod_bgmnames.py <BGM folder> <recording.wav> <runner log> <extract's NOISE folder> [out.h]
Needs numpy.
"""
import glob, hashlib, os, re, sys, wave
import numpy as np

SR = 4000
PIECE, EVERY, PC_SECS, PS1_SECS = 5.0, 5.0, 170.0, 44.0


def load(p, start=0.0, secs=12.0):
    w = wave.open(p)
    r, n, c = w.getframerate(), w.getnframes(), w.getnchannels()
    a0 = min(int(start * r), n)
    w.setpos(a0)
    a = np.frombuffer(w.readframes(min(int(secs * r), n - a0)), '<i2').astype(np.float64).reshape(-1, c).mean(1)
    return np.interp(np.arange(0, len(a) / r, 1 / SR), np.arange(len(a)) / r, a)


class Recordings:
    """The PS1 recordings, their spectra and running energies, for normalised correlation."""

    def __init__(self, sigs):
        self.keys = list(sigs)
        self.len = max(len(s) for s in sigs.values())
        self.m = int(PIECE * SR)
        self.nfft = 1 << int(np.ceil(np.log2(self.len + self.m)))
        X = np.zeros((len(self.keys), self.len))
        for i, k in enumerate(self.keys):
            X[i, :len(sigs[k])] = sigs[k]
        self.F = np.fft.rfft(X, self.nfft, axis=1)
        s1 = np.concatenate([np.zeros((len(X), 1)), np.cumsum(X, 1)], 1)
        s2 = np.concatenate([np.zeros((len(X), 1)), np.cumsum(X * X, 1)], 1)
        m = self.m
        self.energy = np.sqrt(np.maximum(s2[:, m:] - s2[:, :-m] - (s1[:, m:] - s1[:, :-m]) ** 2 / m, 1e-6))
        self.energy[self.energy < 50 * np.sqrt(m)] = np.inf     # silence matches nothing

    def scores(self, piece):
        p = piece - piece.mean()
        p /= np.linalg.norm(p) + 1e-9
        c = np.fft.irfft(self.F * np.conj(np.fft.rfft(p, self.nfft)), self.nfft, axis=1)[:, :self.len - self.m + 1]
        return (np.abs(c) / self.energy).max(1)


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    bgm, rec, log, noise = sys.argv[1:5]
    out = sys.argv[5] if len(sys.argv) > 5 else os.path.join(os.path.dirname(__file__), '..', '..', 'port', 'src', 'mods', 'tables', 'modfs_pc_bgm.h')
    text = open(log).read()
    f0 = int(re.search(r'savestate: loaded .*?, frame (\d+)', text).group(1))
    starts = [(int(f), int(s), int(m)) for f, s, m in re.findall(r'\[\s*(\d+)\] plugin: seqrec: set (\d+) music(\d) from', text)]
    ends = [int(f) for f in re.findall(r'\[\s*(\d+)\] plugin: seqrec: (?:set \d+ music\d from|done)', text)]
    sigs = {}
    for f, s, m in starts:
        nxt = min([e for e in ends if e > f] + [f + int(PS1_SECS * 60)])
        sigs[(s, m)] = load(rec, (f - f0) / 59.94 + 0.2, min(PS1_SECS, (nxt - f) / 59.94 - 0.5))
    same = {}                                    # sequence -> the ones with the same bytes
    for p in glob.glob(os.path.join(noise, '*', 'music*.seq')):
        b, m = os.path.basename(os.path.dirname(p)), os.path.basename(p)
        same.setdefault(hashlib.md5(open(p, 'rb').read()).hexdigest(), []).append((int(b) - 0xE6, int(m[5])))
    group = {}
    for g in same.values():
        for k in g:
            group[k] = tuple(sorted(g))
    R = Recordings(sigs)
    ranked = {}
    for p in sorted(glob.glob(os.path.join(bgm, '*'))):
        if not p.lower().endswith('.wav'):
            continue
        P = load(p, 0, PC_SECS)
        if len(P) < (PIECE + 4) * SR:
            continue
        best = np.zeros(len(R.keys))
        for s in np.arange(0, len(P) / SR - PIECE, EVERY):
            piece = P[int(s * SR):int(s * SR) + R.m]
            if piece.std() > 50:
                best = np.maximum(best, R.scores(piece))
        by = {}
        for i, k in enumerate(R.keys):
            g = group.get(k, (k,))
            by[g] = max(by.get(g, 0.0), float(best[i]))
        ranked[os.path.basename(p).upper()] = sorted(((sc, g) for g, sc in by.items()), reverse=True)
    take = {}
    for n, r in ranked.items():
        if r[0][0] >= 0.35 and r[0][0] - r[1][0] >= 0.15:
            take[n] = r[0][1]
    pair = re.compile(r'(\d+)-BGM(?:(-WITH-M)|(\d))?\.WAV$')

    def half(n):                                 # (room, which sequence the name says)
        m = pair.match(n)
        if not m:
            return None
        return m.group(1), 2 if m.group(2) or m.group(3) == '2' else 1
    for n, r in ranked.items():
        if n in take or not half(n):
            continue
        room, mine = half(n)
        for o, g in list(take.items()):
            ho = half(o)
            if not ho or ho[0] != room or ho[1] == mine:
                continue
            for s, m in g:
                if m != ho[1]:
                    continue
                want = group.get((s, mine), ((s, mine),))
                if any(gg == want and sc >= 0.35 for sc, gg in r[:3]):
                    take[n] = want
    score = {n: dict((g, sc) for sc, g in ranked[n])[take[n]] for n in take}
    first = {}                                   # a sequence -> its best-matching PC WAV
    for n in sorted(take, key=lambda n: -score[n]):
        first.setdefault(take[n], n)
    with open(out, 'w', newline='\n') as o:
        o.write('/* The PC version\'s BGM WAVs that are recordings of a room sound set\'s sequence: PC name\n'
                ' * (PSX_DATA\'s), NOISE.BIN block (0xE6 + set), music number; a sequence the disc has in\n'
                ' * more than one set (the same bytes) has a row for each. alt 1: another PC room\'s WAV\n'
                ' * of a sequence a WAV with alt 0 matches better (used when a mod has only this one).\n'
                ' * Made by tools/mod_tables/mod_bgmnames.py from recordings of every sequence (tools/plugins/seqrec.c). */\n')
        o.write('static const struct { const char *pc; int block, music, alt; } modfs_pc_bgm[] = {\n')
        for n in sorted(take):
            for s, mus in take[n]:
                o.write('    {"BGM/%s", %d, %d, %d},   /* score %.2f */\n' % (n, 0xE6 + s, mus, first[take[n]] != n, score[n]))
        o.write('};\n')
    print('%d of %d PC music WAVs matched -> %s' % (len(take), len(ranked), out))
    for n in sorted(ranked):
        r = ranked[n]
        print('  %-22s %-28s best %s %.2f, next %s %.2f' % (n, ('-> ' + ' '.join('set %d music%d' % k for k in take[n])) if n in take else 'not clear',
                                                          r[0][1], r[0][0], r[1][1], r[1][0]))


if __name__ == '__main__':
    main()
