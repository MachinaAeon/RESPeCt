"""Shared helpers for the overlay bundles ENEMPROG.BIN and STAGPROG.BIN.

A bundle is a run of overlays, each starting on a 2 KB disc sector with its
overlay ID as the first word.  Some overlays follow the ID with a table of
entry points, others go straight into code, so the ID sequence is what marks
a header: IDs only ever increase through a bundle (with occasional gaps).
"""
import struct

SECTOR = 0x800


def words(d):
    return struct.unpack_from(f"<{len(d) // 4}I", d)


def find_overlays(d):
    """Return [(start, end, overlay_id, has_table)] for every overlay in a bundle."""
    w = words(d)
    heads = []
    for o in range(0, len(d), SECTOR):
        x = w[o // 4]
        prev = heads[-1][1] if heads else None
        if prev is None:
            ok = 0 < x < 0x100
        else:
            ok = prev < x <= prev + 4      # next ID, allowing small gaps
        if ok:
            heads.append((o, x))
    out = []
    for i, (o, x) in enumerate(heads):
        end = heads[i + 1][0] if i + 1 < len(heads) else len(d)
        has_table = 0x80000000 <= w[o // 4 + 1] < 0x80200000
        out.append((o, end, x, has_table))
    return out


def table_len(d, start):
    """Number of entry-point words after the ID word (0 if none)."""
    w = words(d)
    n = 0
    while 0x80000000 <= w[start // 4 + 1 + n] < 0x80200000:
        n += 1
    return n
