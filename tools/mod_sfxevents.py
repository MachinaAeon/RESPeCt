#!/usr/bin/env python3
"""The PC version's sound effects that are other recordings than the PS1's,
routed by the event they play for to the PS1 sound that plays there.

    python tools/mod_sfxevents.py <PC PSX_DATA> <NOISE.BIN> <KERNEL.BIN> <TEST.BIN> [out.h]

gun_bio (the PC version) is a port of the PS1 code (C:\\code\\PC_GUN_BIO_2\\PSX\\):
where the PS1 plays sound n of a bank (snd_Cue: the bank's VAB, program
n >> 4, tone n & 15), the PC builds a WAV name from the same numbers:
  - enemies: wav/enemy/<kind>/snd-NNN.wav, NNN the PS1's sound number in
    the kind's bank (gun_bio 00471260); the bank is the kind's NOISE.BIN
    block (TEST's table 0x800A22BC + 0x14; the zombies' are chosen at random
    on the PS1 from 8, the PC has two: the ones their recordings match, 30
    and 33). All 45 pairs that tools/mod_sfxnames.py finds by recording
    follow this.
  - doors: wav/door/XY-n.wav, XY the door's scene file (DOORNEW/DOOR_XY),
    n its sound (gun_bio 004711c0); the PS1 loads block 0x3C + XY into
    bank 6 for the door's scene (room_change.c step 9, snd_ReloadBankSix).
  - footsteps: wav/foot/NNN-fL.wav and -fR.wav, NNN the PC's room (its
    stageNNNN() loads the PS1 stage and room in the table below), f the
    floor (gun_bio 004721c0); the PS1 plays sound f (0x800BB194) of the
    room's bank 6, block 0x10E + 0x800A2364[stage] + room, one sound for
    both feet.
  - keys and menus: wav/key1/key-NNN.wav: sound NNN of bank 0 (block 0).
A sound used by more than one of these (the same waveform: the same
footstep in many rooms, an enemy's repeated sounds) gets, inside a mod,
the PC file with the lowest rank: the PS1's own recordings in
modfs_pc_sfx.h, then enemies, keys, footsteps (left foot first), doors, by
name. Each file still goes to the bank sound it names, unless a better
ranked one names that too.
Weapons, items and the rooms' event sounds aren't routed: the PS1 plays
them from code not yet followed. Writes names and numbers only."""
import hashlib, os, re, struct, sys

# PC room (stageNNNN()) -> PS1 (stage, room): gun_bio's room functions each call 00472A10 with them.
PC_ROOMS = [(7, 4), (7, 0), (0, 0), (0, 4), (0, 2), (0, 1), (0, 5), (0, 6), (9, 0), (9, 1), (9, 2), (9, 3), (8, 1), (8, 2), (8, 3),
            (8, 0), (7, 1), (7, 2), (7, 3), (14, 0), (14, 2), (14, 1), (14, 3), (14, 4), (13, 0), (13, 2), (13, 3), (13, 6), (13, 4),
            (13, 5), (12, 0), (12, 1), (12, 3), (12, 2), (12, 4), (12, 1), (12, 5), (12, 6), (12, 10), (12, 7), (12, 8), (12, 9),
            (12, 12), (12, 13), (3, 14), (3, 13), (3, 15), (3, 16), (3, 0), (3, 2), (3, 3), (3, 5), (3, 4), (3, 1), (3, 8), (3, 7),
            (3, 9), (3, 10), (3, 11), (3, 12), (4, 1), (4, 2), (4, 3), (4, 1), (4, 4), (4, 5), (10, 0), (10, 1), (10, 2), (10, 3),
            (10, 4), (15, 3), (15, 2), (15, 1), (11, 0), (11, 1), (11, 3), (11, 4), (11, 5), (11, 6), (11, 7), (11, 2), (11, 8),
            (16, 0), (16, 1), (16, 2), (16, 3), (16, 4), (16, 5), (16, 6), (16, 7), (16, 8), (16, 9), (16, 10), (16, 11), (1, 0),
            (1, 1), (1, 2), (1, 3), (1, 4), (6, 0), (5, 0), (2, 0), (2, 1), (2, 2), (2, 3), (2, 4), (2, 5), (2, 6), (2, 7), (2, 8),
            (2, 9), (2, 10), (2, 11), (2, 12), (2, 13), (2, 14), (2, 15), (2, 16), (17, 0), (17, 1), (17, 2)]
# PC enemy folder -> PS1 enemy kind (gun_bio 00471260's switch); None: the block itself.
ENEMIES = {'ZOMBIE0': ('block', 30), 'ZOMBIE1': ('block', 33), 'DOG': 11, 'CROW': 12, 'RICKER': 13, 'GOKIBURI': 15,
           'SPIDER': 16, 'CROCODILE': 18, 'IVY': 19, 'TAIRANT': 22, 'GIANTMOTH': 26, 'HUNTER': 28, 'HANK': 30,
           'CHO0': 31, 'CHO1': 32, 'CHO2': 33}
TEST_BASE = 0x8006C118
NOISE_TABLE = 0x80058D2C


class Noise:
    def __init__(self, noise, kernel):
        self.d = open(noise, 'rb').read()
        k = open(kernel, 'rb').read()
        ko = NOISE_TABLE - struct.unpack_from('<I', k, 0x18)[0] + 0x800
        self.v = []
        while True:
            s = struct.unpack_from('<I', k, ko + 4 * len(self.v))[0]
            if self.v and (s < self.v[-1] or s * 2048 > len(self.d)):
                break
            self.v.append(s)
            if s * 2048 == len(self.d):
                break

    def bank(self, blk):
        """(program -> [vag per tone], [vag bytes, 1-based]) of a block's VAB, or None."""
        if blk + 1 >= len(self.v):
            return None
        d, at = self.d, self.v[blk] * 2048
        if at >= self.v[blk + 1] * 2048:
            return None
        c = struct.unpack_from('<I', d, at)[0]
        if not 1 <= c < 64:
            return None
        offs = struct.unpack_from('<%dI' % c, d, at + 4)
        h = at + offs[0]
        if d[h:h + 4] != b'pBAV':
            return None
        ps, ts, vs = struct.unpack_from('<HHH', d, h + 0x12)
        progs, idx = {}, 0
        for p in range(128):
            nt = d[h + 0x20 + p * 16]
            if not nt:
                continue
            ta = h + 0x20 + 2048 + idx * 512
            progs[p] = [struct.unpack_from('<h', d, ta + t * 32 + 22)[0] for t in range(nt)]
            idx += 1
            if idx >= ps:
                break
        sizes = struct.unpack_from('<256H', d, h + 0x20 + 2048 + ps * 512)
        body, vags, o = at + offs[-1], [None], 0
        for j in range(1, vs + 1):
            n = sizes[j] * 8
            vags.append(d[body + o:body + o + n])
            o += n
        return progs, vags


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    pc_root, noise, kernel, test = sys.argv[1:5]
    out = sys.argv[5] if len(sys.argv) > 5 else os.path.join(os.path.dirname(__file__), '..', 'port', 'src', 'modfs_pc_sfx_events.h')
    N = Noise(noise, kernel)
    t = open(test, 'rb').read()
    rd = lambda a: struct.unpack_from('<i', t, a - TEST_BASE)[0]
    stage_banks = [rd(0x800A2364 + 4 * k) for k in range(18)]
    kind_files = [rd(0x800A22BC + 4 * k) for k in range(0x22)]
    recorded = {}
    src = os.path.join(os.path.dirname(out), 'modfs_pc_sfx.h')
    for name, b, v in re.findall(r'\{"([^"]+)", (\d+)u, (\d+)u\}', open(src).read()):
        recorded[name] = (int(b), int(v))
    files = {}
    for root, _, names in os.walk(pc_root):
        for n in names:
            rel = os.path.relpath(os.path.join(root, n), pc_root).replace('\\', '/').upper()
            if rel.endswith('.WAV'):
                files[rel] = True
    banks = {}

    def sound(blk, nib):
        if blk not in banks:
            banks[blk] = N.bank(blk)
        b = banks[blk]
        if not b:
            return None
        progs, vags = b
        tones = progs.get(nib >> 4, [])
        if (nib & 15) >= len(tones) or not 1 <= tones[nib & 15] < len(vags):
            return None
        return blk, tones[nib & 15], hashlib.md5(vags[tones[nib & 15]]).hexdigest()
    rows, why = [], {}
    for rel in sorted(files):
        if rel in recorded:
            continue
        m = re.fullmatch(r'WAV/ENEMY/(\w+)/SND-(\d{3})\.WAV', rel)
        if m and m.group(1) in ENEMIES:
            k = ENEMIES[m.group(1)]
            blk = k[1] if isinstance(k, tuple) else (kind_files[k] + 0x14 if kind_files[k] >= 0 else None)
            r = sound(blk, int(m.group(2))) if blk is not None else None
            rows.append((rel, 'enemy', r))
            continue
        m = re.fullmatch(r'WAV/DOOR/([0-9A-F]{2})-(\d)\.WAV', rel)
        if m:
            rows.append((rel, 'door', sound(0x3C + int(m.group(1), 16), int(m.group(2)))))
            continue
        m = re.fullmatch(r'WAV/FOOT/(\d{3})-(\d)([LR])\.WAV', rel)
        if m and int(m.group(1)) < len(PC_ROOMS):
            st, rm = PC_ROOMS[int(m.group(1))]
            rows.append((rel, 'foot', sound(0x10E + stage_banks[st] + rm, int(m.group(2)))))
            continue
        m = re.fullmatch(r'WAV/KEY1/KEY-(\d{3})\.WAV', rel)
        if m:
            rows.append((rel, 'key', sound(0, int(m.group(1)))))
    # a rank per waveform: the recordings first, then enemies, keys, footsteps (left foot first), doors, by name
    order = {'enemy': 0, 'key': 1, 'foot': 2, 'door': 3}
    ranks, rank = {}, {}
    for name, (b, v) in sorted(recorded.items()):
        sound(b, 0)
        if banks.get(b) and v < len(banks[b][1]):
            h = hashlib.md5(banks[b][1][v]).hexdigest()
            ranks[h] = ranks.get(h, 0) + 1
    for rel, kind, r in sorted((x for x in rows if x[2]), key=lambda x: (order[x[1]], x[0].replace('R.WAV', 'Z.WAV'))):
        rank[rel] = ranks.get(r[2], 0)
        ranks[r[2]] = rank[rel] + 1
    kept = [(rel, kind, r) for rel, kind, r in rows if r]
    with open(out, 'w', newline='\n') as o:
        o.write('/* The PC version\'s sound effects that are other recordings than the PS1\'s, by the event\n'
                ' * they play for: the PS1 sound (NOISE.BIN block, waveform) that plays there. Written by\n'
                ' * tools/mod_sfxevents.py (its comment has how each family is routed). rank: of the PC\n'
                ' * files of one waveform, the lowest wins inside a mod (0: the first). No sound data. */\n')
        o.write('static const struct { const char *pc; uint32_t block, vag, rank; } modfs_pc_sfx_events[] = {\n')
        for rel, kind, (b, v, h) in kept:
            o.write('    {"%s", %du, %du, %du},\n' % (rel, b, v, rank[rel]))
        o.write('};\n')
    by = {}
    for rel, kind, r in rows:
        by.setdefault(kind, [0, 0])[0 if r else 1] += 1
    print('%d PC sound effects routed by event -> %s' % (len(kept), out))
    for k, (a, b) in sorted(by.items()):
        print('  %-6s %4d routed, %d not (no such sound in the bank)' % (k, a, b))
    for rel, kind, r in rows:
        if not r:
            print('  not routed:', rel)


if __name__ == '__main__':
    main()
