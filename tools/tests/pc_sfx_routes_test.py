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

# Cross-version numeric action IDs are not event identity evidence.
ROUTES = (("DOG/SND-001.WAV", 23, 2),)
UNPROVEN = ("DOG/SND-003.WAV", "DOG/SND-005.WAV", "DOG/SND-006.WAV",
            "DOG/SND-008.WAV", "DOG/SND-009.WAV", "CROW/SND-002.WAV",
            "CROW/SND-003.WAV", "CROW/SND-004.WAV", "RICKER/SND-004.WAV",
            "RICKER/SND-007.WAV")


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

    def test_unproven_and_cross_family_cues_stay_unsupported(self):
        for path in UNPROVEN + ("RICKER/SND-005.WAV", "RICKER/SND-006.WAV", "RICKER/SND-008.WAV", "HUNTER/SND-004.WAV", "DOG/SND-004.WAV"):
            with self.subTest(path=path):
                self.assertIsNone(self.catalog.lookup("WAV/ENEMY/" + path))
        self.assertEqual(self.catalog.lookup("WAV/ENEMY/DOG/SND-001.WAV")["targets"], ["sound:23:sfx:2"])

    def test_production_loader_registers_only_proven_targets(self):
        cc = shlex.split(os.environ.get("CC", "clang"), posix=os.name != "nt")
        with tempfile.TemporaryDirectory(prefix="respect-sfx-native-") as tmp:
            out = Path(tmp)
            source = out / "routes.c"
            harness = (ROOT / "tools/tests/modfs_test.c").as_posix()
            source.write_text('#define main established_test_main\n#include "' + harness + '"\n#undef main\n' + r'''
int main(void) {
    FILE *f=fopen("cue.wav","wb"); check(f!=NULL,"audio fixture");
    fwrite("RIFF",1,4,f); fclose(f);
    const char *names[]={"DOG/SND-003", "DOG/SND-005", "DOG/SND-006", "DOG/SND-008", "DOG/SND-009", "CROW/SND-002", "CROW/SND-003", "CROW/SND-004", "RICKER/SND-004", "RICKER/SND-007"};
    for(unsigned i=0;i<sizeof names/sizeof names[0];i++) {
        char rel[256];snprintf(rel,sizeof rel,"PSX_DATA/WAV/ENEMY/%s.WAV",names[i]);
        apply("route-test",rel,"cue.wav");
        check(nsfx==0,"unproven cross-version alias cannot register a replacement");
    }
    apply("route-test","WAV/ENEMY/DOG/SND-001.WAV","cue.wav");
    check(nsfx==1 && sfxs[0].block==23 && sfxs[0].vag==2,"independently proven step route retained");
    apply("route-test","NOISE/21/9.WAV","cue.wav");
    check(nsfx==2 && sfxs[1].block==21 && sfxs[1].vag==9,"explicit native waveform route remains available");
    puts("PASS: unproven aliases rejected, native route and verified dog step retained");
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
