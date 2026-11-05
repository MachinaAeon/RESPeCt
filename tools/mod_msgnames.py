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
CONTEXT, CONTEXT_CLOSE = 0.70, 0.15              # inside the room the rest of its PC group is surely in
# The game's own door messages (TEST.BIN, room_change.c n_op_Door), and the PC's
# wording where it differs: room 240's PC group (0015) has "It's now open." where
# the PS1 says "You've unlocked the door." when that door opens from this side.
GAME_STRINGS = [(0x800A2ECC, None), (0x800A2E84, None), (0x800A2F14, "it s now open")]
USED_PREFIX, MSG_TAIL = 0x800A2EF4, [0x79, 0xFC00, 0xFE3C, 0xFE00, 0xFE20, 0xFF01, 0xFE00]
ITEM_NAMES = 0x800A6D5C
GLYPHS = ([" "] + [""] * 11 + list("0123456789:;,") + ['"', "!", "?", "!?"]
          + [chr(c) for c in range(65, 91)] + ["(", "/", ")", "'", "-", "."] + [chr(c) for c in range(97, 123)])


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


def test_codes(t, addr):                       # TEST.BIN's codes at addr, to the end mark (excluded)
    out = []
    o = addr - TEST_BASE
    while o + 2 <= len(t):
        c = struct.unpack_from('<H', t, o)[0]
        if c == 0xFFFF:
            return out
        out.append(c)
        o += 2
    return out


def codes_text(codes):
    return norm(''.join(GLYPHS[c] if c < len(GLYPHS) else (' ' if c in (0xFFFE, 0) else '.' if c == 0x79 else '') for c in codes))


def crc(codes):
    return zlib.crc32(b''.join(struct.pack('<H', c) for c in codes))


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
    group_rooms = collections.defaultdict(collections.Counter)
    for name in sorted(pc):
        scored = sorted(((score(pc[name], t), k) for k, t in ps1.items()), reverse=True)
        best, k = scored[0]
        exact = best == 1.0 and all(s < 1.0 or ps1[kk] == ps1[k] for s, kk in scored)
        if not exact and (best < SCORE or any(ps1[kk] != ps1[k] for s, kk in scored if s >= best - CLOSE)):
            continue
        claims[ps1[k]].add(pc[name])
        picks.append((name, ps1[k], best))
        blocks = {kk[0] for s, kk in scored if ps1[kk] == ps1[k]}
        if len(blocks) == 1:                     # only one room has this text: the PC group's room
            group_rooms[name.split('/')[-1][:4]][k[0]] += 1
    picked = {p[0] for p in picks}
    for name in sorted(pc):                      # the PC's rewordings, inside the room their group is in
        g = name.split('/')[-1][:4]
        if name in picked or len(group_rooms[g]) != 1:
            continue
        room = next(iter(group_rooms[g]))
        scored = sorted(((score(pc[name], t), k) for k, t in ps1.items() if k[0] == room), reverse=True)
        if not scored:
            continue
        best, k = scored[0]
        second = next((x for x, kk in scored[1:] if ps1[kk] != ps1[k]), 0)
        if best >= CONTEXT and best - second >= CONTEXT_CLOSE:
            claims[ps1[k]].add(pc[name])
            picks.append((name, ps1[k], best))
    room_of = {g: next(iter(c)) for g, c in group_rooms.items() if len(c) == 1}
    blocks_of = collections.defaultdict(set)
    for k, tt in ps1.items(): blocks_of[tt].add(k[0])
    # for a text several pictures show: the one from that room's PC group first, then the closest
    picks.sort(key=lambda p: (room_of.get(p[0].split('/')[-1][:4]) not in blocks_of[p[1]], -p[2], p[0]))
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
    t = open(test, 'rb').read()
    game_rows = []                               # the game's own door messages
    for addr, pc_words in GAME_STRINGS:
        codes = test_codes(t, addr)
        want = pc_words or codes_text(codes)
        name = next((n for n in sorted(pc) if pc[n] == want), None)
        if name:
            game_rows.append((name, addr, len(codes) + 1, crc(codes + [0xFFFF])))
    used_rows = []                               # "You've used the <key>.", by the key's item number
    prefix = test_codes(t, USED_PREFIX)
    names = {}
    for item in range(64):
        ptr = struct.unpack_from('<I', t, ITEM_NAMES - TEST_BASE + 4 * item)[0]
        if TEST_BASE <= ptr < TEST_BASE + len(t):
            names[item] = test_codes(t, ptr)
    for n in sorted(pc):
        m = re.match(r'you ve used (.+)$', pc[n])
        if not m:
            continue
        cands = sorted(((score(m[1], codes_text(c)), i) for i, c in names.items() if codes_text(c)), reverse=True)
        if cands and cands[0][0] >= SCORE and (len(cands) < 2 or cands[0][0] - cands[1][0] >= CLOSE) \
                and all(r[1] != cands[0][1] for r in used_rows):
            item = cands[0][1]
            stream = prefix + [0xFFFE, 0xFC01] + names[item] + MSG_TAIL
            used_rows.append((n, item, len(stream), crc(stream)))
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
        for name, b, x, w, c, best in rows:
            o.write('    {"%s", %du, %du, %du, 0x%08Xu},   /* %.3f */\n' % (name, b, x, w, c, best))
        o.write('};\n/* The game\'s own messages (TEST.BIN strings, by address: the doors\'), and the\n'
                ' * doors\' "You\'ve used the <key>." by the key\'s item number (its stream in\n'
                ' * 0x800A88C8 as n_op_Door builds it): picture, id, halfwords, CRC32. */\n'
                'static const PcPickupProfile modfs_pc_system[] = {\n')
        for name, addr, w, c in game_rows or [(None, 0, 0, 0)]:
            o.write('    {%s, 0x%08Xu, %du, 0x%08Xu},\n' % ('"%s"' % name if name else 'NULL', addr, w, c))
        o.write('};\nstatic const PcPickupProfile modfs_pc_used_keys[] = {\n')
        for name, item, w, c in used_rows or [(None, 0, 0, 0)]:
            o.write('    {%s, %du, %du, 0x%08Xu},\n' % ('"%s"' % name if name else 'NULL', item, w, c))
        o.write('};\n#endif\n')
    print('%d PC pictures read as text, %d PS1 messages get one (%d pictures), %d game strings, %d keys used; %s written'
          % (len(pc), len(rows), len({r[0] for r in rows}), len(game_rows), len(used_rows), out))


if __name__ == '__main__':
    main()
