#!/usr/bin/env python3
"""Inspect/import PC asset packages without running code or altering originals.

Recognition uses the runtime's checked-in mapping tables. It establishes a
loading route, never successful decoding, memory fit or gameplay acceptance.
Unknown resources block import; no filename-derived message IDs are invented.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import sys
import tempfile
import pc_noise

ROOT = Path(__file__).resolve().parents[1]
PICTURES = {".BMP", ".PNG", ".JPG", ".JPEG", ".TGA"}
CODE = {".EXE", ".DLL", ".ASI", ".IPS", ".BPS", ".XDELTA", ".PY", ".LUA", ".BAT", ".CMD", ".PS1"}
RESERVED = re.compile(r"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)", re.I)


class PackageError(ValueError):
    pass


def canonical_resource(name):
    name = str(name).replace("\\", "/")
    parts = name.split("/")
    if (not name or len(name.encode("utf-8")) >= 512 or not name.isascii()
            or any(not p or p in {".", ".."} or ":" in p or p[-1:] in {".", " "}
                   or RESERVED.match(p) or any(ord(c) < 32 for c in p) for p in parts)):
        raise PackageError("unsafe, non-ASCII or overlong resource path")
    if parts[0].upper() == "PSX_DATA":
        parts = parts[1:]
    if not parts:
        raise PackageError("PSX_DATA must contain a resource")
    return "/".join(parts).upper()


class Catalog:
    """Read, rather than duplicate, the existing production alias tables."""
    def __init__(self, source):
        self.paths = {}
        self.disc = {}
        self.noise_profile = pc_noise.read_profile(source / "modfs_pc_noise.h")
        self.pc_bgm = {n: (int(b), int(m)) for n, b, m in re.findall(
            r'\{"([^"]+)", (\d+), (\d+)\}', (source / "modfs_pc_bgm.h").read_text(encoding="utf-8"))}
        for name, lba, size, digest in re.findall(
                r'\{"([^"]+)", (\d+)u, (\d+)u, 0x([0-9A-F]+)ull\}',
                (source / "disc_ids.h").read_text(encoding="utf-8")):
            self.disc[name] = (int(size), int(digest, 16))
        for header, kind, pattern in [
            ("modfs_pc.h", "loose", r'\{"([^"]+)", "([^"]+)", (\d+)u, (\d+)u,'),
            ("modfs_bmp.h", "picture", r'\{"([^"]+)", "([^"]+)", (\d+)u, (\d+)u,'),
            ("modfs_pack.h", "packed", r'\{"([^"]+)", "([^"]+)", (\d+)u, (-?\d+), (\d+)u, (\d+)u,'),
            ("modfs_wav.h", "stream-audio", r'\{"([^"]+)", "([^"]+)", (\d+)\}')]:
            text = (source / header).read_text(encoding="utf-8")
            rows = re.findall(pattern, text)
            if not rows:
                raise PackageError("missing or unrecognized production table: " + header)
            for row in rows:
                path, archive, *numbers = row
                target = kind + ":" + archive + ":" + ":".join(numbers[:-1] if kind != "stream-audio" else numbers)
                entry = self.paths.setdefault(path, {"kind": kind, "targets": [], "archives": [], "size_limits": []})
                # The runtime tries loose, pictures, then packed aliases, in this order.
                if entry["kind"] != kind:
                    continue
                entry["targets"].append(target)
                entry["archives"].append(archive)
                if kind in {"loose", "packed"}:
                    entry["size_limits"].append(int(numbers[-1]))

        text = (source / "modfs_pc_sfx.h").read_text(encoding="utf-8")
        for path, block, vag in re.findall(r'\{"([^"]+)", (\d+)u, (\d+)u\}', text):
            self.paths[path] = {"kind": "sound", "targets": [f"sound:{int(block)}:sfx:{int(vag)}"], "archives": ["NOISE.BIN"]}

        text=(source / "modfs_pc_messages.h").read_text(encoding="utf-8")
        for path,block,index,words,crc in re.findall(r'\{"([^"]+)", (\d+)u, (\d+)u, (\d+)u, 0x([0-9A-F]+)u\}',text):
            self.paths[path]={"kind":"pc-message","targets":[f"message:{block}:{index}"],"archives":["ROOMENV.DAT"],
                              "native_stream_words":int(words),"native_stream_crc32":crc,"canvas":[512,512]}
        for table,target in (("modfs_pc_system","game-message"),("modfs_pc_used_keys","door-used")):
            body=re.search(table+r'\[\] = \{(.*?)\n\};',text,re.S)
            for path,ident,words,crc in re.findall(r'\{"([^"]+)", (0x[0-9A-F]+|\d+)u, (\d+)u, 0x([0-9A-F]+)u\}',body.group(1) if body else ""):
                self.paths.setdefault(path,{"kind":"pc-message","targets":[],"archives":["TEST.BIN"],
                                  "native_stream_words":int(words),"native_stream_crc32":crc,"canvas":[512,512]})
                self.paths[path]["targets"].append(f"{target}:{int(ident,0)}")

        text=(source / "modfs_pc_pickups.h").read_text(encoding="utf-8")
        for path,item,words,crc in re.findall(r'\{"([^"]+)", (\d+)u, (\d+)u, 0x([0-9A-F]+)u\}',text):
            self.paths[path]={"kind":"pc-pickup","targets":[f"pickup:{item}"],"archives":[],
                              "native_stream_words":int(words),"native_stream_crc32":crc,"canvas":[512,512]}

        text=(source / "modfs_pc_diaries.h").read_text(encoding="utf-8")
        self.diaries={int(pc): (int(native),int(fg),int(bg)) for pc,native,fg,bg in
                      re.findall(r'\{(\d+)u, (\d+)u, (\d+)u, (\d+)u\}',text)}

    def lookup(self, name):
        if name == "NOISE/NOISE.BIN":
            return {"kind": "pc-noise", "targets": ["archive:NOISE.BIN"], "archives": ["NOISE.BIN"]}
        entry = self.paths.get(name)
        if not entry and Path(name).suffix in PICTURES:
            entry = self.paths.get(name.rsplit(".", 1)[0] + ".BMP")
        if entry:
            return dict(entry)
        enemy = re.fullmatch(r"NAB/ENEM(\d\d)\.(NAB|NAC|BMP|PNG|JPG|JPEG|TGA)", name)
        if enemy and int(enemy[1]) <= 37:
            number = int(enemy[1]); number = 16 if 34 <= number <= 35 else 17 if number >= 36 else number
            kind = enemy[2] if enemy[2] in {"NAB", "NAC"} else "PICTURE"
            archives = ["OBJ.DAT"] if number < 30 else ["EV_OBJ.DAT"]
            if number == 33:
                archives.append("OBJ.DAT")
            return {"kind": "enemy", "targets": [f"enemy:{number}:{kind}"], "archives": archives}
        if name in {"TEXT/" + x + ".TXT" for x in ["ROOMS", "GAME", "ITEMS", "CHARS", "UI", "LIST"]}:
            archive = "ROOMENV.DAT" if name == "TEXT/ROOMS.TXT" else "B/BIGTIM.BIN" if name == "TEXT/ITEMS.TXT" else "TEST.BIN"
            return {"kind": "text", "targets": [name], "archives": [archive]}
        font = re.fullmatch(r"FONT/FONT([0-9A-F]+)\.(BMP|PNG|JPG|JPEG|TGA)", name)
        if font and 0x186 <= int(font[1], 16) <= 0x188:
            return {"kind": "font", "targets": [f"FONT/FONT{int(font[1], 16):X}"], "archives": ["EV_OBJ.DAT"]}
        picture = re.fullmatch(r"BIGTIM/([0-9]+)\.(BMP|PNG|JPG|JPEG|TGA)", name)
        if picture and int(picture[1]) < 64:  # own_picture's fixed block arrays
            return {"kind": "item-picture", "targets": [f"BIGTIM/{int(picture[1])}"], "archives": ["B/BIGTIM.BIN"]}
        page = re.fullmatch(r"DIARY/([0-9]+)/([0-9]+)\.(BMP|PNG|JPG|JPEG|TGA)", name)
        if page and int(page[1]) < 13 and 1 <= int(page[2]) <= 90:
            return {"kind": "diary", "targets": [f"DIARY/{int(page[1])}/{int(page[2])}"], "archives": ["CONFFILE.BIN"]}
        if name=="DIARY/BOOK.FLC":
            return {"kind":"pc-book","targets":["pc-diary-book"],"archives":[],"max_bytes":8*1024*1024}
        if re.fullmatch(r"DIARY/KEY-AB\.(BMP|PNG|JPG|JPEG|TGA)",name):
            return {"kind":"pc-diary-keys","targets":["pc-diary-keys"],"archives":[],"canvas":[320,240],"max_bytes":8*1024*1024}
        title=re.fullmatch(r"DIARY/D-NAME([0-9]{2})\.(BMP|PNG|JPG|JPEG|TGA)",name)
        if title and int(title[1]) in self.diaries:
            native=self.diaries[int(title[1])][0]
            return {"kind":"pc-diary-title","targets":[f"pc-diary-title:{native}"],"archives":[],"canvas":[640,480],"max_bytes":8*1024*1024}
        diary = re.fullmatch(r"DIARY/FILE([0-9A-F]{2})/FILE([0-9A-F]{2})_([0-9A-F]|B[0-9A-F]?)\.(BMP|PNG|JPG|JPEG|TGA)", name)
        if diary and diary[1]==diary[2] and int(diary[1],16) in self.diaries:
            number=int(diary[1],16);native,fg,bg=self.diaries[number];part=diary[3]
            background=part.startswith("B");index=int(part[1:] or "0",16) if background else int(part,16)
            valid=(bg==1 and part=="B" or bg>1 and len(part)==2 and index<bg) if background else index<fg
            if valid:
                return {"kind":"pc-diary", "targets":[f"pc-diary:{native}:"+ ("background" if background else "foreground")+f":{index}"],
                        "archives":["CONFFILE.BIN"], "diary_pc":number,"diary_native":native,"diary_foregrounds":fg,"diary_backgrounds":bg,
                        "canvas":[512,448 if background else 512],"max_bytes":8*1024*1024}
        if name in self.pc_bgm:                  # a PC music WAV with a measured room sequence
            block, music = self.pc_bgm[name]
            return {"kind": "sound", "targets": [f"sound:{block}:music:{music}"], "archives": ["NOISE.BIN"]}
        sound = re.fullmatch(r"NOISE/([0-9]+)/(?:([0-9]+)\.WAV|MUSIC([0-9]+)\.(WAV|SEQ))", name)
        if sound and all(int(x) <= 0xffffffff for x in [sound[1], sound[2] or sound[3]]):
            return {"kind": "sound", "targets": [f"sound:{int(sound[1])}:" + ("sfx:" + str(int(sound[2])) if sound[2] else "music:" + str(int(sound[3])))], "archives": ["NOISE.BIN"]}
        video = re.fullmatch(r"AVI/([^/]+)\.(AVI|MPG|MPEG|MP4|MKV|WEBM|MOV|WMV|M4V)", name) or             re.fullmatch(r"(M/[^/]+\.STR)/VIDEO\.(AVI|MPG|MPEG|MP4|MKV|WEBM|MOV|WMV|M4V)", name)
        if video:
            target = video[1] if video[1].startswith("M/") else "M/" + video[1] + ".STR"
            if target in self.disc:
                return {"kind": "movie-video", "targets": ["movie-video:" + target], "archives": [target]}
        stream = name.rsplit("/", 1)
        if len(stream) == 2 and stream[0] in self.disc and stream[0].endswith(".STR"):
            frame = re.fullmatch(r"([0-9]+)\.(WAV|BMP|PNG|JPG|JPEG|TGA)", stream[1])
            if frame and (int(frame[1]) <= 31 if frame[2] == "WAV" else 1 <= int(frame[1]) <= 0xffffffff):
                kind = "stream-audio" if frame[2] == "WAV" else "movie-frame"
                return {"kind": kind, "targets": [kind + ":" + stream[0] + ":" + str(int(frame[1]))], "archives": [stream[0]]}
        if name in self.disc:
            size, digest = self.disc[name]
            if digest or name.endswith(".STR"):
                return {"blocked": "game code or whole CD stream cannot be replaced as an asset"}
            return {"kind": "archive", "targets": ["archive:" + name], "archives": [name],
                    "max_bytes": (size + 2047) // 2048 * 2048}
        return None


def file_digest(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
        after = os.fstat(stream.fileno())
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise PackageError("source changed while it was being inspected")
    return digest.hexdigest(), after.st_size


def structural_error(path, name, size):
    if not size:
        return "empty resource"
    with path.open("rb") as stream:
        head = stream.read(128)
        ext = Path(name).suffix
        if ext == ".BMP":
            if len(head) < 54 or head[:2] != b"BM":
                return "truncated or invalid BMP header"
            declared, offset = struct.unpack_from("<I", head, 2)[0], struct.unpack_from("<I", head, 10)[0]
            dib, w, h, planes, bits, compression = struct.unpack_from("<IiiHHI", head, 14)
            if dib < 40 or w <= 0 or not h or planes != 1 or bits not in {1, 4, 8, 16, 24, 32} or offset < 14 + dib or offset >= size or declared > size:
                return "invalid BMP layout"
            if compression in {0, 3} and offset + ((w * bits + 31) // 32 * 4) * abs(h) > size:
                return "truncated BMP pixel data"
        elif ext == ".PNG":
            if len(head) < 33 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR" or not all(struct.unpack_from(">II", head, 16)):
                return "truncated or invalid PNG header"
        elif ext in {".JPG", ".JPEG"} and head[:2] != b"\xff\xd8":
            return "invalid JPEG header"
        elif ext == ".TGA" and (len(head) < 18 or not all(struct.unpack_from("<HH", head, 12))):
            return "truncated or invalid TGA header"
        elif ext==".FLC":
            if len(head)<128 or struct.unpack_from('<I5H',head)!=(size,0xaf12,18,196,82,8):
                return "book requires18 indexed196x82 FLC frames and a complete declared file"
        elif ext == ".WAV":
            if size < 44 or head[:4] != b"RIFF" or head[8:12] != b"WAVE":
                return "invalid WAV header"
            offset, fmt, data_bytes = 12, None, None
            while offset + 8 <= size:
                stream.seek(offset)
                tag, length = struct.unpack("<4sI", stream.read(8))
                if length > size - offset - 8:
                    return "truncated WAV chunk"
                if tag == b"fmt ":
                    if length < 16:
                        return "truncated WAV format"
                    fmt = struct.unpack("<HHIIHH", stream.read(16))
                if tag == b"data":
                    data_bytes = length
                offset += 8 + length + (length & 1)
            if not fmt or data_bytes is None:
                return "WAV has no format or sample data"
            codec, channels, rate, _, _, bits = fmt
            if codec != 1 or channels not in {1, 2} or bits != 16 or not rate:
                return "WAV must be 16-bit PCM, mono or stereo"
            if not data_bytes or data_bytes % (2 * channels):
                return "WAV has no complete PCM frames"
        elif ext == ".NAC":
            if len(head) < 4:
                return "truncated NAC header"
            keys = struct.unpack_from("<I", head)[0]
            if not 1 <= keys <= 13108 or 4 + keys * 112 + 4 > size:
                return "NAC key count exceeds file or original command format"
            stream.seek(4 + keys * 112)
            commands = struct.unpack("<I", stream.read(4))[0]
            if 8 + keys * 112 + commands * 8 != size:
                return "NAC command count does not match file size"
            # enemy_whole uses every boss key; nac_into drops the ordinary
            # enemies' trailing sentinel. Keep their two consumers distinct.
            boss = re.fullmatch(r"NAB/ENEM(31|32|33)\.NAC", name) is not None
            available_keys = keys if boss else keys - 1
            for i in range(commands):
                word, key = struct.unpack("<II", stream.read(8))
                if i and key != 0xffffffff and key >= available_keys:
                    return "NAC command refers to a missing key"
        elif ext == ".TXT":
            stream.seek(0)
            try:
                stream.read().decode("utf-8-sig")
            except UnicodeDecodeError:
                return "text must use UTF-8"
    return None


def is_link(st):
    return stat.S_ISLNK(st.st_mode) or bool(getattr(st, "st_file_attributes", 0) & 0x400)


def inspect_package(source, catalog):
    source = Path(source)
    if is_link(source.lstat()) or not source.is_dir():
        raise PackageError("source must be an ordinary directory")
    rows = []
    prefix = source.name + "/" if source.name.upper() in {"RESOURCE", "NAB"} else ""

    def walk(folder):
        with os.scandir(folder) as listing:
            entries = sorted(listing, key=lambda e: (e.name.casefold(), e.name))
        for entry in entries:
            path = Path(entry.path)
            rel = path.relative_to(source).as_posix()
            row = {"source": rel, "status": "blocked", "reason": "", "validation": "not-run"}
            try:
                st = entry.stat(follow_symlinks=False)
                if is_link(st) or not (stat.S_ISDIR(st.st_mode) or stat.S_ISREG(st.st_mode)):
                    raise PackageError("links, reparse points and special files are not imported")
                if stat.S_ISDIR(st.st_mode):
                    canonical_resource(rel + "/_")
                    walk(path)
                    continue
                row["destination"] = prefix + rel
                row["resource"] = canonical_resource(prefix + rel)
                name = row["resource"]
                row["sha256"], row["bytes"] = file_digest(path)
                if "/" not in rel and re.fullmatch(r"(?:README|LICENSE|COPYING|CHANGELOG)(?:\.(?:TXT|MD|RST))?", name):
                    row.update(status="ignored", reason="package documentation")
                elif Path(name).suffix in CODE:
                    row["reason"] = "PC executable/code patch requires a separate port"
                else:
                    item = catalog.lookup(name)
                    if item and "blocked" in item:
                        row["reason"] = item["blocked"]
                    elif not item:
                        row["reason"] = ("missing verified message mapping" if name.startswith(("MESSAGE/", "EVENTMSG/", "ITEM/PIC")) else
                                         "PC diary pagination needs a verified page adaptation" if name.startswith("DIARY/FILE") else
                                         "no production resource mapping")
                    else:
                        row.update(item)
                        error = structural_error(path, name, row["bytes"])
                        if item["kind"] in {"pc-message", "pc-diary", "pc-diary-title", "pc-diary-keys", "pc-pickup"}:
                            # The verified PC raster layout uses a complete square canvas.
                            # Alternate encodings still require runtime decode/size acceptance.
                            if Path(name).suffix == ".BMP":
                                head=path.read_bytes()[:54]
                                if len(head)<54 or struct.unpack_from("<ii",head,18) not in {tuple(item["canvas"]),(item["canvas"][0],-item["canvas"][1])}:
                                    error="verified PC picture requires its %dx%d canvas" % tuple(item["canvas"])
                            elif Path(name).suffix == ".PNG":
                                head=path.read_bytes()[:33]
                                if len(head)<33 or struct.unpack_from(">II",head,16)!=tuple(item["canvas"]):
                                    error="verified PC picture requires its %dx%d canvas" % tuple(item["canvas"])
                        if item["kind"] == "pc-noise":
                            try:
                                row["changed_banks"] = [r["block"] for r in pc_noise.preflight(path.read_bytes(), catalog.noise_profile)]
                                if not row["changed_banks"]:
                                    # Runtime keeps every native byte for this no-op reference.
                                    row["targets"], row["archives"] = [], []
                            except ValueError as exc:
                                error = str(exc)
                        if row["bytes"] > row.get("max_bytes", sys.maxsize):
                            error = "PC diary image exceeds its 8 MiB encoded-file limit" if item["kind"].startswith("pc-diary") else "whole archive exceeds its original sector allocation"
                        if item["kind"] == "packed" and any(row["bytes"] > n for n in item["size_limits"]):
                            error = "packed part exceeds the original converter's supported size"
                        if error:
                            row["reason"] = error
                        else:
                            row.update(status="recognized", reason="loading route found; runtime decoding, memory and gameplay still require verification", validation="runtime-required")
            except (OSError, PackageError) as exc:
                row["reason"] = str(exc)
            rows.append(row)

    walk(source)
    # Do not import only the recognised fraction of a document. Filenames give
    # verified document/slot identities; companions give its complete layout.
    for pc,(native,fg,bg) in catalog.diaries.items():
        group=[r for r in rows if r.get("kind")=="pc-diary" and r["diary_pc"]==pc]
        if not group:continue
        present={target for r in group if r["status"]=="recognized" for target in r["targets"]}
        expected={f"pc-diary:{native}:foreground:{i}" for i in range(fg)} | {f"pc-diary:{native}:background:{i}" for i in range(bg)}
        if present!=expected:
            for row in group:
                if row["status"]=="recognized":row.update(status="blocked",reason="complete PC diary requires all verified foreground/background companions in the package")
    # Resolve aliases before copying; never let filesystem enumeration choose a winner.
    owners = defaultdict(list)
    for i, row in enumerate(rows):
        if row["status"] == "recognized":
            for target in row["targets"]:
                owners[target].append(i)
    for indices in owners.values():
        indices = list(dict.fromkeys(indices))
        if len(indices) < 2:
            continue
        first = rows[indices[0]]
        if all(rows[i]["sha256"] == first["sha256"] and rows[i]["targets"] == first["targets"] for i in indices):
            for i in indices[1:]:
                rows[i].update(status="duplicate", reason="byte-identical alias of " + first["source"])
        else:
            for i in indices:
                rows[i].update(status="blocked", reason="conflicting aliases for " + first["resource"])
    for whole in rows:
        if whole.get("kind") not in {"archive", "pc-noise"}:
            continue
        overlaps = [r for r in rows if r is not whole and r["status"] in {"recognized", "duplicate"}
                    and set(whole["archives"]) & set(r.get("archives", []))]
        if overlaps:
            whole.update(status="blocked", reason="whole archive overlaps individual replacements")
            for row in overlaps:
                row.update(status="blocked", reason="individual resource overlaps whole archive replacement")
    counts = Counter(r["status"] for r in rows)
    return {"schema": 1, "runtime_verified": False,
            "summary": {k: counts[k] for k in ["recognized", "blocked", "duplicate", "ignored"]}, "files": rows}


def import_package(source, destination, catalog):
    source, destination = Path(source), Path(destination)
    if os.path.lexists(destination):
        raise PackageError("destination already exists; nothing was overwritten")
    src, dst = source.resolve(), destination.resolve()
    if dst == src or src in dst.parents:
        raise PackageError("destination must be outside the source directory")
    report = inspect_package(source, catalog)
    if report["summary"]["blocked"] or not report["summary"]["recognized"]:
        raise PackageError("package has blocked resources or no recognized assets; nothing was imported")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="_pc-import-", dir=destination.parent) as temporary:
        stage = Path(temporary) / "package"
        stage.mkdir()
        for row in report["files"]:
            if row["status"] not in {"recognized", "ignored"}:
                continue
            origin, target = source / row["source"], stage / row["destination"]
            if is_link(origin.lstat()) or origin.resolve() != src / row["source"]:
                raise PackageError("source path changed after inspection")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(origin, target)
            if file_digest(target)[0] != row["sha256"]:
                raise PackageError("source content changed after inspection; nothing was imported")
        (stage / "compatibility-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if os.path.lexists(destination):
            raise PackageError("destination appeared during import; nothing was overwritten")
        stage.rename(destination)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["check", "import"])
    parser.add_argument("source", type=Path, help="PC mod root, or its PSX_DATA directory")
    parser.add_argument("destination", nargs="?", type=Path, help="new mods/<name> folder, for import only")
    parser.add_argument("--report", type=Path, help="optional JSON report outside the source directory")
    args = parser.parse_args(argv)
    try:
        if args.command == "import" and args.destination is None:
            raise PackageError("import requires a new destination folder")
        if args.command == "check" and args.destination is not None:
            raise PackageError("check does not take a destination")
        if args.report and (args.report.resolve() == args.source.resolve() or args.source.resolve() in args.report.resolve().parents):
            raise PackageError("report must be outside the source directory")
        catalog = Catalog(ROOT / "port/src")
        report = inspect_package(args.source, catalog)
        if args.report:
            with args.report.open("x", encoding="utf-8") as out:
                json.dump(report, out, indent=2); out.write("\n")
        for row in report["files"]:
            if row["status"] == "blocked":
                print(f'BLOCKED {row["source"]}: {row["reason"]}')
        print("; ".join(f"{v} {k}" for k, v in report["summary"].items()))
        print("Recognition is not runtime or gameplay verification.")
        if args.command == "import":
            import_package(args.source, args.destination, catalog)
            print("Imported into " + str(args.destination))
        return 2 if report["summary"]["blocked"] or not report["summary"]["recognized"] else 0
    except (PackageError, OSError) as exc:
        print("ERROR: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
