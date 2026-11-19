#!/usr/bin/env python3
"""Build a Spanish-text mod from your own Spanish PAL disc (SLES-02732).

    python pal_spanish.py "Resident Evil Survivor (Spain).bin" mods/espanol

The Spanish release has the same data layout as the USA one, so its data
files (room messages, item descriptions, the font, translated pictures)
go into the mod whole, by their disc names. The item names live in the
game's code, which can't be swapped, so they're read out of the PAL
TEST.BIN and written as text/GAME.txt in the USA entries' order.

Nothing from the game is in this script: it only copies from the disc you
give it, into a mods folder on your own computer.

The Spanish font puts its letters where the USA one has German and French
ones; text/CHARS.txt maps them, so ROOMS.txt / GAME.txt can be written
and edited with real Spanish letters.
"""
import os
import struct
import sys

# The data files the Spanish release changed (its code files can't be
# replaced; their text comes in through text/*.txt instead).
DATA_FILES = [
    "AEX.DAT", "B/BIGTIM.BIN", "B/MAPTIM.BIN", "B/STGBG2.BIN", "B/STGBG10.BIN",
    "CONFFILE.BIN", "EV_OBJ.DAT", "ITEMDAT.DAT", "OBJ.DAT", "OPTTIM.BIN",
    "ROOMENV.DAT", "SCR_TIM.BIN", "TITTIM.BIN", "TYPE_TIM.BIN",
]

# The font's cells, as the USA font names them (port/src/mods/modfs_texts.inc).
GLYPHS = [" ", "{_}", "▸", "{L2}", "{R2}", "{L1}", "{R1}", "△", "○", "×", "□", "▾",
          "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ";", ",", "\"", "!", "?", "⁉"] + \
         [chr(c) for c in range(ord("A"), ord("Z") + 1)] + ["(", "/", ")", "'", "-", "·"] + \
         [chr(c) for c in range(ord("a"), ord("z") + 1)] + \
         ["Ä", "ä", "Ö", "ö", "Ü", "ü", "ß", "À", "à", "Â", "â", "È", "è", "É", "é", "Ê", "ê", "Ï", "ï",
          "Î", "î", "Ô", "ô", "Ù", "ù", "Û", "û", "Ç", "ç", "{S.}", "{T.}", "{A.}", "{R.}", "“", ".", "…",
          "{dash1}", "{dash2}", "+", "=", "&"]
assert len(GLYPHS) == 128

# Where the Spanish font differs: cell -> its letter.
SPANISH = {87: "Á", 88: "á", 89: "Í", 90: "í", 91: "Ó", 92: "ó", 93: "Ú", 94: "ú", 95: "¿", 96: "¡",
           97: "Ñ", 98: "ñ", 102: "º", 103: "ª", 104: "«", 105: "»", 106: "Ü", 107: "ü"}
NAMES = list(GLYPHS)
for g, ch in SPANISH.items():
    NAMES[g] = ch

PAL_TEST_BASE = 0x8006C108     # where the Spanish TEST.BIN loads (16 bytes below the USA one)
PAL_NAME_PTRS = 0x3AE04          # PAL TEST.BIN: the item names' pointers
# USA GAME.txt entry -> the pointer slot that uses it (the same slots in both).
ENTRY_SLOT = {1: 0, 2: 1, 3: 2, 4: 3, 5: 4, 6: 5, 7: 6, 8: 7, 9: 8, 10: 10, 11: 11, 12: 12, 13: 13,
              14: 14, 15: 15, 16: 16, 17: 20, 18: 21, 19: 22, 20: 23, 21: 24, 22: 30, 23: 31, 24: 32,
              25: 33, 26: 34, 27: 35, 28: 36, 29: 37, 30: 38, 31: 39, 33: 41, 34: 43, 35: 44, 36: 45,
              37: 46, 38: 47, 39: 48, 40: 49, 41: 52, 42: 53, 43: 54, 44: 55, 45: 64, 46: 65, 47: 66,
              48: 67, 49: 68, 50: 69, 51: 70, 52: 71, 53: 72, 54: 73, 55: 74, 56: 75, 57: 76, 58: 108,
              59: 111}


# The menus' strings the Spanish disc changed, which live in its code files:
# (file, USA address, Spanish disc's address, kind). Kinds: "w" a 16-bit text,
# copied; "a" the same with its accents dropped (a screen with the USA font);
# "n" an 8-bit small-font text, turned into the USA small font's codes
# (no accents); "b" a line the Spanish disc merged into the one before, blank.
SCREENS = [
    ("OPTION.BIN", 0x800CC480, 0x800CC610, "n"), ("OPTION.BIN", 0x800CC48C, 0x800CC61C, "n"),
    ("OPTION.BIN", 0x800CC494, 0x800CC71C, "n"), ("OPTION.BIN", 0x800CC984, 0x800CCA9C, "n"),
    ("OPTION.BIN", 0x800CCB24, 0x800CCC3C, "n"), ("OPTION.BIN", 0x800CCB2B, 0x800CCC43, "n"),
    ("OPTION.BIN", 0x800CCB32, 0x800CCC4A, "n"), ("OPTION.BIN", 0x800CC8C0, 0x800CC9B0, "w"),
    ("OPTION.BIN", 0x800CC8CC, 0x800CC9D4, "w"), ("OPTION.BIN", 0x800CC8D4, 0x800CC9DC, "b"),
    ("OPTION.BIN", 0x800CC8E4, 0x800CC9E8, "w"), ("OPTION.BIN", 0x800CC8FC, 0x800CCA1C, "w"),
    ("OPTION.BIN", 0x800CC90C, 0x800CC9C0, "w"), ("OPTION.BIN", 0x800CC9C4, 0x800CCAC4, "w"),
    ("OPTION.BIN", 0x800CC9D0, 0x800CCADC, "w"), ("OPTION.BIN", 0x800CC9E8, 0x800CCAF4, "w"),
    ("OPTION.BIN", 0x800CCAE0, 0x800CC998, "w"), ("OPTION.BIN", 0x800CCB3C, 0x800CCC54, "w"),
    ("TEST.BIN", 0x800A2ECC, 0x800A3078, "w"), ("TEST.BIN", 0x800A2E84, 0x800A3038, "w"),   # the doors' messages
    ("TEST.BIN", 0x800A2EF4, 0x800A30D8, "w"), ("TEST.BIN", 0x800A2F14, 0x800A30A4, "w"),
    ("SCORE.BIN", 0x8006D92C, 0x8006D90C, "a"), ("SCORE.BIN", 0x8006D93C, 0x8006D924, "a"),   # the results' labels
    ("SCORE.BIN", 0x8006D948, 0x8006D930, "a"), ("SCORE.BIN", 0x8006D960, 0x8006D940, "a"),
    ("SCORE.BIN", 0x8006D978, 0x8006D954, "a"), ("SCORE.BIN", 0x8006D998, 0x8006D964, "a"),
    ("SCORE.BIN", 0x8006D9AC, 0x8006D984, "a"),
    ("TITLE.BIN", 0x8007FCB4, 0x8007FDC0, "w"), ("TITLE.BIN", 0x8007FCC0, 0x8007FDCC, "b"),   # the title screen's options
    ("TITLE.BIN", 0x8007FCD0, 0x8007FDD0, "w"), ("TITLE.BIN", 0x8007FCE0, 0x8007FDE0, "w"),
    ("TITLE.BIN", 0x8007FCE8, 0x8007FDE8, "b"), ("TITLE.BIN", 0x8007FCF8, 0x8007FDF4, "w"),
    ("TITLE.BIN", 0x8007FD10, 0x8007FE28, "w"), ("TITLE.BIN", 0x8007FEB0, 0x8007F828, "w"),
    ("TITLE.BIN", 0x8007FF0C, 0x80080020, "w"), ("TITLE.BIN", 0x8007FD8C, 0x8007FEA0, "n"),
    ("TITLE.BIN", 0x8007FEF4, 0x80080008, "n"), ("TITLE.BIN", 0x8007FEFB, 0x8008000F, "n"),
    ("TITLE.BIN", 0x8007FF02, 0x80080016, "n"),
]
PAL_OPTION_BASE = 0x800C4008     # where the Spanish OPTION.BIN loads
_ACCENTS = "AaIiOoUu?!Nn" "eEe" "oa\"\"Uu" "OoUuUuCc"     # cells 87..114 without accents


def plain_glyph(g):
    """A Spanish font cell with its accent dropped, as a font cell."""
    if not 87 <= g <= 114:
        return g
    c = _ACCENTS[g - 87]
    if c.isupper():
        return 29 + ord(c) - ord("A")
    if c.islower():
        return 61 + ord(c) - ord("a")
    return {"?": 27, "!": 26}.get(c, 25)


def narrow_code(g):
    """A Spanish font cell as the USA small font's code (ASCII - 0x20)."""
    if 87 <= g <= 114:
        c = _ACCENTS[g - 87]
    elif g == 120:
        c = '"'
    elif g in (60, 121, 122):
        c = "."
    elif g < 128 and len(GLYPHS[g]) == 1:
        c = GLYPHS[g]
    else:
        c = " "
    return ord(c) - 0x20


# Words of RESPeCt's own over menu words that are pictures (the Spanish disc
# left those English): text/CAPTIONS.txt. The picture's colour depth, its
# texels from VRAM's corner, the hash of that VRAM, the words.
CAPTIONS = [
    (1, 1426, 270, 1580, 297, 0xE9FA3DB5, "CONTROLES"),
    (1, 1598, 267, 1693, 282, 0xCCC4E8D7, "TIPO DE MANDO"),
    (1, 1454, 330, 1499, 345, 0x162212C3, "SIN USO"),
    (1, 1641, 330, 1686, 345, 0x1DB97D8B, "SIN USO"),
    (1, 1433, 352, 1518, 367, 0x962C3148, "BÚSQUEDA RÁPIDA"),
    (1, 1639, 352, 1689, 367, 0x4B56846B, "APUNTAR"),
    (1, 1454, 372, 1539, 387, 0x345005BE, "AVANZAR/CORRER"),
    (1, 1429, 392, 1493, 407, 0xB5D13750, "GIRAR IZQ."),
    (1, 1500, 392, 1570, 407, 0x6479D9EE, "GIRAR DCHA."),
    (1, 1662, 392, 1707, 407, 0x162212C3, "SIN USO"),
    (1, 1482, 413, 1516, 428, 0x70967DF7, "ATRÁS"),
    (0, 2664, 256, 2728, 272, 0xB4F624C0, "ACEPTAR/ATACAR"),
    (0, 2744, 256, 2808, 272, 0x31A53053, "CANCELAR"),
    (1, 1487, 271, 1650, 288, 0x22C60DF3, "AJUSTAR PANTALLA"),
    (1, 1440, 488, 1632, 504, 0x9E788A01, "PULSA START"),
]

class Disc:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.files = {}
        pvd = self.sec(16)
        if pvd[1:6] != b"CD001":
            sys.exit(f"{path}: not a raw (2352-byte sector) PlayStation disc image")
        root = pvd[156:190]
        self.walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "")

    def sec(self, lba, n=1):
        self.f.seek(lba * 2352)
        raw = self.f.read(2352 * n)
        out = []
        for i in range(n):
            s = raw[i * 2352:(i + 1) * 2352]
            out.append(s[24:24 + 2048] if s[15] == 2 else s[16:16 + 2048])
        return b"".join(out)

    def walk(self, lba, size, path):
        data = self.sec(lba, (size + 2047) // 2048)
        i = 0
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            rec = data[i:i + n]
            i += n
            elba, esz = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
            name = rec[33:33 + rec[32]]
            if name in (b"\x00", b"\x01"):
                continue
            name = name.decode("latin1").split(";")[0]
            if rec[25] & 2:
                self.walk(elba, esz, path + name + "/")
            else:
                self.files[path + name] = (elba, esz)

    def read(self, name):
        lba, size = self.files[name]
        return self.sec(lba, (size + 2047) // 2048)[:size]


def decode(d, o):
    out = []
    while True:
        c = struct.unpack_from("<H", d, o)[0]
        nxt = struct.unpack_from("<H", d, o + 2)[0]
        o += 2
        if c == 0xFFFF:
            return "".join(out)
        if c == 0xFFFE:
            out.append("{n}" if nxt == 0xFFFF else "\n")
        elif c == 0 and nxt in (0xFFFE, 0xFFFF):
            out.append("{sp}")
        elif c < len(NAMES):
            out.append(NAMES[c])
        elif c >> 8 == 0xFC:
            out.append("{color %d}" % (c & 0xFF))
        elif c == 0xFF00:
            out.append("{more}")
        else:
            out.append("{x%04X}" % c)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    disc = Disc(sys.argv[1])
    if "SLES_027.32" not in disc.files:
        sys.exit("That isn't the Spanish disc (SLES-02732).")
    out = sys.argv[2]

    test = disc.read("TEST.BIN")
    h = 0xCBF29CE484222325
    for b in test:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    if h != 0x4DAF01E260EF3EA0:
        sys.exit("This PAL disc doesn't have the Tio Victor Spanish translation (v1.0.1) applied.")
    for name in DATA_FILES:
        dst = os.path.join(out, *name.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(disc.read(name))

    os.makedirs(os.path.join(out, "text"), exist_ok=True)
    with open(os.path.join(out, "text", "GAME.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# Item names, from the Spanish disc's TEST.BIN, in the USA order.\n")
        for entry, slot in sorted(ENTRY_SLOT.items()):
            ptr = struct.unpack_from("<I", test, PAL_NAME_PTRS + 4 * slot)[0] - PAL_TEST_BASE
            f.write(f"@{entry}\n{decode(test, ptr).strip(' ')}\n")
    opt, score, title = disc.read("OPTION.BIN"), disc.read("SCORE.BIN"), disc.read("TITLE.BIN")
    with open(os.path.join(out, "text", "SCREENS.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# The menus' strings from the Spanish disc: @address (USA), then the bytes the game reads.\n")
        for name, usa, es, kind in SCREENS:
            b, o = {"TEST.BIN": (test, es - PAL_TEST_BASE), "SCORE.BIN": (score, es - PAL_TEST_BASE),   # (SCORE and TITLE load where TEST does)
                    "TITLE.BIN": (title, es - PAL_TEST_BASE),
                    "OPTION.BIN": (opt, es - PAL_OPTION_BASE)}[name]
            f.write("@%08X\n" % usa)
            if kind == "b":
                f.write("0000ffff\n")
            elif kind == "n":
                e = b.index(0xFF, o)
                f.write(bytes(c if c >= 0xF0 else narrow_code(c) for c in b[o:e]).hex() + "ff\n")
            else:
                e = o
                while struct.unpack_from("<H", b, e)[0] != 0xFFFF:
                    e += 2
                codes = struct.unpack("<%dH" % ((e + 2 - o) // 2), b[o:e + 2])
                if kind == "a":
                    codes = [plain_glyph(c) for c in codes]
                f.write(struct.pack("<%dH" % len(codes), *codes).hex() + "\n")
    with open(os.path.join(out, "text", "CAPTIONS.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# RESPeCt's own Spanish words over menu words that are pictures: @depth x0 y0 x1 y1 hash, then the words.\n")
        for d, x0, y0, x1, y1, h, t in CAPTIONS:
            f.write("@%d %d %d %d %d %08x\n%s\n" % (d, x0, y0, x1, y1, h, t))
    with open(os.path.join(out, "text", "CHARS.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# The Spanish font's letters (they sit where the USA font has German and French ones).\n")
        for g, ch in sorted(SPANISH.items()):
            f.write(f"{ch} {g}\n")
    print(f"Wrote the Spanish mod to {out}")


if __name__ == "__main__":
    main()
