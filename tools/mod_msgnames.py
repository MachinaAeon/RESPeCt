"""The PC version's message pictures that show the PS1's room messages.

    python tools/mod_msgnames.py <PC PSX_DATA> <extract folder> <TEST.BIN> <ROOMENV.DAT> port/src/modfs_pc_messages.h

The PC draws its messages as pictures (Message/0003-00.BMP), the PS1 as text
(extract writes text/ROOMS.txt). Windows' own OCR (tools/winocr.ps1, offline,
Windows 10/11) reads each picture; its words are compared with every room
message (difflib ratio on letters and digits). A picture goes in when:
  - the closest PS1 message scores at least 0.85,
  - every PS1 message within 0.08 of it has that same text (then the picture
    is that text wherever the PS1 has it: the same room visited again),
  - no other PC picture with different extra words claims the same PS1 text
    ("card key blue" and "card key red" on the PS1's "a card key": neither).
Pictures in other languages (some of the PC's are Chinese) and the 1x1
placeholders read as nothing and stay out. The row carries the PS1 message's
halfword count to its end mark and its CRC32, which the runtime checks before
it draws the picture. Writes identities only, no text or pictures. Needs
Pillow; the OCR runs through PowerShell."""
import collections, difflib, glob, os, re, struct, subprocess, sys, tempfile, zlib

ROOMENV_TABLE, TEST_BASE = 0x800A32F0, 0x8006C118
SCORE, CLOSE = 0.85, 0.08
VERIFIED = [("EVENTMSG/RE-005.BMP", 210, 8)]     # measured before (no English text to read on the others)


def norm(s):
    s = re.sub(r'\{[^}]*\}', ' ', s).replace('“', '"').replace('”', '"').replace('’', "'")
    return ' '.join(re.sub(r'[^a-z0-9]+', ' ', s.lower()).split())


def room_messages(test, roomenv):
    t, d = open(test, 'rb').read(), open(roomenv, 'rb').read()
    v = []
    for i in range(1024):
        v.append(struct.unpack_from('<I', t, ROOMENV_TABLE - TEST_BASE + 4 * i)[0])
        if i and v[-1] == len(d) // 2048:
            break

    def message(b, x):                           # (room_load / modfs.c roomenv_part, roomenv_text)
        at = v[b] * 2048
        a, e = struct.unpack_from('<II', d, at + 20)
        p = d[at + a:at + e]
        c = struct.unpack_from('<H', p, 0)[0]
        o = struct.unpack_from('<h', p, 4 + 4 * x)[0] // 2 * 2
        end = len(p)
        for k in range(c):
            ok = struct.unpack_from('<h', p, 4 + 4 * k)[0] // 2 * 2
            if o < ok < end:
                end = ok
        hw = [struct.unpack_from('<H', p, o + 2 * i)[0] for i in range((end - o) // 2)]
        if 0xFFFF not in hw:
            return None
        w = hw.index(0xFFFF) + 1
        return w, zlib.crc32(p[o:o + 2 * w])
    return message


def ocr(pc_root):
    from PIL import Image, ImageOps
    tmp = tempfile.mkdtemp(prefix='respect-msg-')
    files = []
    for f in sorted(glob.glob(os.path.join(pc_root, 'Message', '*.BMP')) + glob.glob(os.path.join(pc_root, 'eventmsg', '*.bmp'))):
        im = Image.open(f).convert('L')
        if im.size != (512, 512) or not im.getbbox():
            continue
        bb = im.getbbox()
        im = ImageOps.invert(im.crop((max(bb[0] - 8, 0), max(bb[1] - 8, 0), bb[2] + 8, bb[3] + 8)))
        im = im.resize((im.width * 3, im.height * 3), Image.LANCZOS)
        name = os.path.relpath(f, pc_root).replace('\\', '/').upper()
        p = os.path.join(tmp, '%04d.png' % len(files))
        im.save(p)
        files.append((name, p))
    lst, out = os.path.join(tmp, 'list.txt'), os.path.join(tmp, 'ocr.txt')
    open(lst, 'w').write('\n'.join(p for _, p in files))
    subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    os.path.join(os.path.dirname(os.path.abspath(__file__)), 'winocr.ps1'), '-List', lst, '-Out', out], check=True)
    read = {}
    for line in open(out, encoding='utf-8-sig'):
        p, _, text = line.rstrip('\n').partition('\t')
        read[os.path.normcase(p)] = norm(text.replace(' | ', ' '))
    return {name: read.get(os.path.normcase(p), '') for name, p in files}


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    pc_root, extract, test, roomenv, out = sys.argv[1:]
    ps1, cur = {}, None
    for line in open(os.path.join(extract, 'text', 'ROOMS.txt'), encoding='utf-8'):
        m = re.match(r'@(\d+)\.(\d+)\s*$', line)
        if m:
            cur = (int(m[1]), int(m[2]))
            ps1[cur] = ''
        elif cur and not line.startswith('#'):
            ps1[cur] += ' ' + line.strip()
    ps1 = {k: norm(t) for k, t in ps1.items() if len(norm(t)) >= 4}
    message = room_messages(test, roomenv)
    pc = {k: t for k, t in ocr(pc_root).items() if t}
    score = lambda a, b: difflib.SequenceMatcher(None, a, b).ratio()
    picks, claims = [], collections.defaultdict(set)
    for name in sorted(pc):
        scored = sorted(((score(pc[name], t), k) for k, t in ps1.items()), reverse=True)
        best, k = scored[0]
        if best < SCORE or any(ps1[kk] != ps1[k] for s, kk in scored if s >= best - CLOSE):
            continue
        claims[ps1[k]].add(pc[name])
        picks.append((name, ps1[k], best))
    extra = lambda x, t: frozenset(set(x.split()) - set(t.split()))
    bad = {t for t, v in claims.items() if len({extra(x, t) for x in v}) > 1 and any(extra(x, t) for x in v)}
    rows, seen = [], set()
    for name, t, best in picks:
        if t in bad:
            continue
        for k in sorted(k for k, tt in ps1.items() if tt == t):
            m = message(*k)
            if k in seen or not m:
                continue
            seen.add(k)
            rows.append((name, k[0], k[1], m[0], m[1], best))
    for name, b, x in VERIFIED:
        m = message(b, x)
        if m and (b, x) not in seen:
            rows.append((name, b, x, m[0], m[1], 1.0))
            seen.add((b, x))
    with open(out, 'w', newline='\n') as o:
        o.write('/* The PC\'s message pictures that show the PS1\'s room messages, with the\n'
                ' * message\'s halfword count to its end mark and CRC32 (checked before a\n'
                ' * picture is drawn). Written by tools/mod_msgnames.py: Windows\' OCR of each\n'
                ' * picture against text/ROOMS.txt, at least %.2f, nothing different within\n'
                ' * %.2f, no two PC texts with other extra words on one PS1 text. Identities\n'
                ' * only: no message text or image data. PC canvas is 512 x 512.\n'
                ' * See docs/PC_BITMAPS.md. */\n' % (SCORE, CLOSE))
        o.write('#ifndef MODFS_PC_MESSAGES_H\n#define MODFS_PC_MESSAGES_H\n#define MODFS_PC_AUTOMATIC 1\n'
                'typedef struct { const char *pc; uint32_t block, id, words, crc; } PcMessageProfile;\n'
                'static const PcMessageProfile modfs_pc_messages[] = {\n')
        for name, b, x, w, crc, best in rows:
            o.write('    {"%s", %du, %du, %du, 0x%08Xu},   /* %.3f */\n' % (name, b, x, w, crc, best))
        o.write('};\n#endif\n')
    print('%d PC pictures read as text, %d PS1 messages get one (%d pictures), %s written'
          % (len(pc), len(rows), len({r[0] for r in rows}), out))


if __name__ == '__main__':
    main()
