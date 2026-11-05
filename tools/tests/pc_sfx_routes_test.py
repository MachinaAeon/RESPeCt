"""Asset-free regressions for verified and rejected PC sound routes.

Run with Python 3 and the existing native compiler (CC may override clang).
The C check includes the production loader via its established test harness;
no game executable, disc, WAV or generated game code is needed.
"""
from pathlib import Path
import os
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import pc_mod

# Routes: the PS1's own recordings (tools/mod_sfxnames.py: the same sound by
# cross-correlation) and, for the PC's other recordings, the event: the PC
# file's number is the PS1 sound number in the kind's bank (gun_bio 00471260,
# a port of the PS1 code), the bank's VAB gives its waveform
# (tools/mod_sfxevents.py). All 45 recorded pairs follow that table, the
# repeated tones too (zombie 5/7/9/11, dog 15).
ROUTES = (("DOG/SND-001.WAV", 23, 2), ("DOG/SND-003.WAV", 23, 4), ("CROW/SND-004.WAV", 22, 5),
          ("ZOMBIE1/SND-002.WAV", 33, 3))
BY_EVENT = (("DOG/SND-006.WAV", 23, 7), ("CROW/SND-002.WAV", 22, 3), ("CROW/SND-003.WAV", 22, 4),
            ("RICKER/SND-004.WAV", 21, 5), ("RICKER/SND-007.WAV", 21, 8), ("RICKER/SND-012.WAV", 21, 6),
            ("HUNTER/SND-004.WAV", 36, 4))
NO_SUCH_SOUND = ("CHO2/SND-000.WAV", "DOG/SND-016.WAV")   # (no bank for CHO2 on the PS1; the dog's has 16 sounds)


def pcm(value=0):
    body = struct.pack("<hh", value, value)
    return (b"RIFF" + struct.pack("<I", 36 + len(body)) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, 1, 22050, 44100, 2, 16) +
            b"data" + struct.pack("<I", len(body)) + body)


class PcSfxRoutesTests(unittest.TestCase):
    def setUp(self):
        self.catalog = pc_mod.Catalog(ROOT / "port/src")

    def test_event_routes_use_the_original_native_targets(self):
        for path, block, vag in ROUTES:
            with self.subTest(path=path):
                pc = self.catalog.lookup("WAV/ENEMY/" + path)
                self.assertIsNotNone(pc)
                native = self.catalog.lookup(f"NOISE/{block}/{vag}.WAV")
                self.assertEqual(pc["targets"], native["targets"])
                self.assertEqual(pc["targets"], [f"sound:{block}:sfx:{vag}"])
                self.assertEqual(pc["archives"], ["NOISE.BIN"])

    def test_each_new_alias_conflicts_with_its_native_waveform(self):
        for path, block, vag in ROUTES:
            with self.subTest(path=path), tempfile.TemporaryDirectory(prefix="respect-sfx-conflict-") as tmp:
                root = Path(tmp)
                for name, value in (("PSX_DATA/WAV/ENEMY/" + path, 1), (f"NOISE/{block}/{vag}.WAV", 2)):
                    out = root / name
                    out.parent.mkdir(parents=True, exist_ok=True)
                    out.write_bytes(pcm(value))
                report = pc_mod.inspect_package(root, self.catalog)
                self.assertEqual(report["summary"]["blocked"], 2)
                self.assertTrue(all("conflicting aliases" in row["reason"] for row in report["files"]))

    def test_identical_native_and_pc_aliases_copy_once(self):
        with tempfile.TemporaryDirectory(prefix="respect-sfx-duplicate-") as tmp:
            root = Path(tmp) / "source"
            root.mkdir()
            for name in ("pSx_DaTa/wAv/eNeMy/dOg/sNd-001.wAv", "NOISE/023/02.WAV"):
                out = root / name
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(pcm())
            report = pc_mod.import_package(root, Path(tmp) / "imported", self.catalog)
            self.assertEqual(report["summary"]["recognized"], 1)
            self.assertEqual(report["summary"]["duplicate"], 1)
            self.assertEqual(report["summary"]["blocked"], 0)
            self.assertFalse(report["runtime_verified"])
            self.assertEqual(sum(p.is_file() and p.suffix.upper() == ".WAV" for p in (Path(tmp) / "imported").rglob("*")), 1)
            self.assertEqual((root / "NOISE/023/02.WAV").read_bytes(), pcm())

    def test_other_recordings_go_by_their_event(self):
        for path, block, vag in BY_EVENT:
            with self.subTest(path=path):
                self.assertEqual(self.catalog.lookup("WAV/ENEMY/" + path)["targets"], [f"sound:{block}:sfx:{vag}"])
        for path in NO_SUCH_SOUND:
            with self.subTest(path=path):
                self.assertIsNone(self.catalog.lookup("WAV/ENEMY/" + path))
        self.assertEqual(self.catalog.lookup("WAV/ENEMY/DOG/SND-001.WAV")["targets"], ["sound:23:sfx:2"])
        self.assertEqual(self.catalog.lookup("WAV/DOOR/00-0.WAV")["targets"], ["sound:60:sfx:1"])
        self.assertEqual(self.catalog.lookup("WAV/FOOT/000-0L.WAV")["targets"], ["sound:343:sfx:1"])
        # weapons: bank 1 is block id + 1; the shot is sound 5 (launchers 3), the casing 1, the
        # handguns' magazine-in 3, the dry fire 0 (block 1's tones 5/1/3 are waveforms 7/6/1)
        for path, block, vag in (("GUN-A-ATTACK", 1, 7), ("GUN-A-BACK", 1, 6), ("GUN-A-RELOAD", 1, 1),
                                 ("GURENEDO-ATTACK", 9, 3), ("GURENEDO-NO", 9, 5), ("SHORTGUN-RELOAD", 5, 3)):
            with self.subTest(path=path):
                self.assertEqual(self.catalog.lookup(f"WAV/WEAP/{path}.WAV")["targets"], [f"sound:{block}:sfx:{vag}"])

    def test_production_loader_registers_recorded_and_event_targets(self):
        cc = shlex.split(os.environ.get("CC", "clang"), posix=os.name != "nt")
        with tempfile.TemporaryDirectory(prefix="respect-sfx-native-") as tmp:
            out = Path(tmp)
            source = out / "routes.c"
            harness = (ROOT / "tools/tests/modfs_test.c").as_posix()
            source.write_text('#define main established_test_main\n#include "' + harness + '"\n#undef main\n' + r'''
int main(void) {
    FILE *f=fopen("cue.wav","wb"); check(f!=NULL,"audio fixture");
    fwrite("RIFF",1,4,f); fclose(f);
    apply("route-test","PSX_DATA/WAV/ENEMY/CHO2/SND-000.WAV","cue.wav");
    check(nsfx==0,"a PC sound with no PS1 sound registers nothing");
    apply("route-test","WAV/ENEMY/DOG/SND-001.WAV","cue.wav");
    check(nsfx==1 && sfxs[0].block==23 && sfxs[0].vag==2 && sfxs[0].rank==0,"recorded dog step route retained");
    apply("route-test","PSX_DATA/WAV/ENEMY/DOG/SND-006.WAV","cue.wav");
    check(nsfx==2 && sfxs[1].block==23 && sfxs[1].vag==7,"the dog's sound 6 by its event");
    apply("route-test","PSX_DATA/WAV/FOOT/000-0R.WAV","cue.wav");
    check(nsfx==3 && sfxs[2].block==343 && sfxs[2].vag==1 && sfxs[2].rank>0,"the right foot: the room's one step sound, ranked after the left");
    apply("route-test","NOISE/21/9.WAV","cue.wav");
    check(nsfx==4 && sfxs[3].block==21 && sfxs[3].vag==9,"explicit native waveform route remains available");
    puts("PASS: recorded and event routes, native route kept, no route without a PS1 sound");
    return 0;
}
''', encoding="utf-8")
            exe = out / ("routes.exe" if os.name == "nt" else "routes")
            subprocess.run([*cc, "-O1", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                            *["-I" + str(ROOT / p) for p in ("port/include", "port/src", "port/third_party")],
                            str(source), str(ROOT / "port/src/modfs_memory.c"),str(ROOT/"port/src/modio.c"), "-o", str(exe), "-lm"], check=True)
            subprocess.run([str(exe)], cwd=out, check=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
