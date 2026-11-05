"""The PC version's loose sound effects that are the PS1's own recordings.

    python tools/mod_sfxnames.py <PC PSX_DATA> <extract folder> port/src/modfs_pc_sfx.h

The PC plays its sound effects from loose WAVs (wav/enemy/dog/snd-001.wav),
the PS1 from its sound banks (extract writes them: NOISE/023/02.wav). Where
a PC file is the same recording as one of the banks' sounds, a mod's
replacement of it goes there. Both are resampled to 11025 Hz mono; a pair is
taken when the normalized cross-correlation at its best lag is at least
0.94, no other PS1 sound comes within 0.08 of it, and the lengths agree
within 0.8-1.25 (the PC's are often a little longer). Anything else is left
out: the PC's door, footstep and weapon sounds are other recordings.
Writes names and numbers only, no sound data. Needs numpy."""
import glob, hashlib, os, sys, wave
import numpy as np

RATE = 11025
SCORE, MARGIN, LENGTHS = 0.94, 0.08, (0.8, 1.25)


def load(path):
    try:
        w = wave.open(path)
    except (wave.Error, EOFError):
        return None
    n, ch, sw, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    if sw != 2 or not n:
        return None
    a = np.frombuffer(w.readframes(n), '<i2').astype(np.float32)
    if ch > 1:
        a = a.reshape(-1, ch).mean(1)
    if len(a) < 16 or np.abs(a).max() < 1:
        return None
    t = np.arange(0, len(a) * RATE / rate) * rate / RATE
    return np.interp(t, np.arange(len(a)), a).astype(np.float32)


def spectrum(a):
    n = 1024
    if len(a) < n:
        a = np.pad(a, (0, n - len(a)))
    frames = np.lib.stride_tricks.sliding_window_view(a, n)[::256]
    s = np.abs(np.fft.rfft(frames * np.hanning(n), axis=1)).mean(0)
    edges = np.geomspace(2, len(s), 25).astype(int)
    v = np.log1p(np.array([s[edges[i]:max(edges[i] + 1, edges[i + 1])].mean() for i in range(24)]))
    return v - v.mean()


def correlation(a, b):
    n = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    c = np.fft.irfft(np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n)), n)
    return float(np.abs(c).max() / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    pc_root, extract, out = sys.argv[1:]
    ps1 = {}                                     # one entry per distinct recording, with every bank it's in
    for f in sorted(glob.glob(os.path.join(extract, 'NOISE', '*', '*.wav'))):
        name = os.path.splitext(os.path.basename(f))[0]
        if not name.isdigit():
            continue
        a = load(f)
        if a is None:
            continue
        block = int(os.path.basename(os.path.dirname(f)))
        ps1.setdefault(hashlib.md5(a.tobytes()).hexdigest(), [a, spectrum(a), []])[2].append((block, int(name)))
    if not ps1:
        sys.exit('no NOISE/<block>/<n>.wav in ' + extract + ': run the game once with an extract folder')
    sounds = list(ps1.values())
    specs = np.array([s[1] for s in sounds])
    lengths = np.array([len(s[0]) for s in sounds])
    rows = []
    for f in sorted(glob.glob(os.path.join(pc_root, '**', '*'), recursive=True)):
        rel = os.path.relpath(f, pc_root).replace('\\', '/')
        low = rel.lower()
        if not low.endswith('.wav') or not (low.startswith('wav/') or low.startswith('item/wav/')):
            continue
        if low.startswith(('wav/scene', 'wav/bgm', 'wav/xaff')):   # voices and streams: modfs_wav.h
            continue
        a = load(f)
        if a is None:
            continue
        near = np.linalg.norm(specs - spectrum(a), axis=1) + 2 * np.abs(np.log((lengths + 1) / (len(a) + 1)))
        scored = sorted(((correlation(a, sounds[i][0]), i) for i in np.argsort(near)[:25]), reverse=True)
        best, i = scored[0]
        second = scored[1][0] if len(scored) > 1 else 0.0
        ratio = len(sounds[i][0]) / len(a)
        if best >= SCORE and best - second >= MARGIN and LENGTHS[0] <= ratio <= LENGTHS[1]:
            block, vag = min(sounds[i][2])
            rows.append((rel.upper(), block, vag, best))
    with open(out, 'w', newline='\n') as o:
        o.write('/* The PC version\'s loose sound effects that are the PS1\'s own recordings, and\n'
                ' * the bank sound each one is (written by tools/mod_sfxnames.py from the PC\'s\n'
                ' * PSX_DATA and the extracted banks: the same recording, cross-correlation at\n'
                ' * least %.2f, no other within %.2f, lengths within %.2f-%.2f). A replacement\n'
                ' * goes to that sound and its copies in the other banks. DOG/SND-001 is also\n'
                ' * proven by its event: PC 0043D630 locomotion -> 00471260(11,1); PS1\n'
                ' * cerberus_StepSound -> mono1/spatial12/13 -> NOISE23 waveform 2. No sound data. */\n'
                % (SCORE, MARGIN, LENGTHS[0], LENGTHS[1]))
        o.write('static const struct { const char *pc; uint32_t block, vag; } modfs_pc_sfx[] = {\n')
        for rel, block, vag, best in rows:
            o.write('    {"%s", %du, %du},   /* %.3f */\n' % (rel, block, vag, best))
        o.write('};\n')
    print('%d PC sound effects matched, %s written' % (len(rows), out))


if __name__ == '__main__':
    main()
