"""Asset-free tests of PC package recognition and all-or-nothing import."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import pc_mod


def bmp(w=2,h=2):
    stride=(w*3+3)&~3
    data=bytearray(54+stride*h);data[:2]=b"BM"
    struct.pack_into("<I",data,2,len(data));struct.pack_into("<I",data,10,54)
    struct.pack_into("<IiiHHI",data,14,40,w,h,1,24,0)
    return bytes(data)


def wav():
    b=bytearray(48);b[:4]=b"RIFF";struct.pack_into("<I",b,4,40);b[8:16]=b"WAVEfmt ";struct.pack_into("<IHHIIHH",b,16,16,1,1,22050,44100,2,16);b[36:40]=b"data";struct.pack_into("<I",b,40,4)
    return bytes(b)


class PcModTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="respect-pc-mod-")
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.source = self.base / "source"
        self.source.mkdir()
        self.catalog = pc_mod.Catalog(ROOT / "port/src")

    def put(self, name, data=None):
        p = self.source / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(bmp() if data is None else data)
        return p

    def scan(self):
        return pc_mod.inspect_package(self.source, self.catalog)

    def test_wrapper_and_case_are_recognized(self):
        self.put("pSx_DaTa/mAp/MAP00_1.bmp")
        row = self.scan()["files"][0]
        self.assertEqual((row["status"], row["resource"]), ("recognized", "MAP/MAP00_1.BMP"))
        self.assertEqual(row["validation"], "runtime-required")

    def test_pc_shapes_and_animations_use_original_consumer(self):
        self.put("nab/ENEM00.NAB", b"opaque-model")
        keys = 2
        self.put("nab/ENEM00.NAC", struct.pack("<I", keys) + bytes(112 * keys) + struct.pack("<I", 1) + bytes(8))
        rows = self.scan()["files"]
        self.assertTrue(all(x["status"] == "recognized" for x in rows))
        self.assertTrue(all(x["validation"] == "runtime-required" for x in rows))

    def test_bad_nac_is_blocked(self):
        self.put("nab/ENEM00.NAC", struct.pack("<I", 200000))
        self.assertEqual(self.scan()["files"][0]["status"], "blocked")

    def test_unknown_text_picture_never_gets_guessed(self):
        self.put("PSX_DATA/Message/0003-01.bmp")
        row = self.scan()["files"][0]
        self.assertEqual(row["status"], "blocked")
        self.assertIn("mapping", row["reason"])

    def test_pc_diary_requires_page_identity(self):
        self.put("PSX_DATA/diary/file00/01.bmp")
        row = self.scan()["files"][0]
        self.assertEqual(row["status"], "blocked")
        self.assertIn("pagination", row["reason"])

    def test_code_patch_is_blocked(self):
        self.put("d3d8.dll", b"MZ")
        self.assertEqual(self.scan()["files"][0]["status"], "blocked")

    def test_same_target_in_flat_and_wrapped_package_conflicts(self):
        self.put("PSX_DATA/map/MAP00_1.bmp")
        self.put("map/MAP00_1.png", b"not-png")
        self.assertGreater(self.scan()["summary"]["blocked"], 0)

    def test_identical_aliases_are_explicit_duplicates(self):
        self.put("PSX_DATA/map/MAP00_1.bmp")
        self.put("map/MAP00_1.bmp")
        report = self.scan()
        self.assertEqual(report["summary"]["recognized"], 1)
        self.assertEqual(report["summary"]["duplicate"], 1)

    def test_two_different_files_for_one_resource_are_both_blocked(self):
        self.put("PSX_DATA/map/MAP00_1.bmp")
        changed = bytearray(bmp()); changed[-1] = 42
        self.put("map/MAP00_1.bmp", bytes(changed))
        report = self.scan()
        self.assertEqual(report["summary"]["blocked"], 2)
        self.assertEqual(report["summary"]["recognized"], 0)

    def test_unmapped_file_is_accounted_for(self):
        self.put("unknown/data.bin", b"unknown")
        self.put("README.txt", b"description")
        report = self.scan()
        self.assertEqual(report["summary"]["blocked"], 1)
        self.assertEqual(report["summary"]["ignored"], 1)
        self.assertEqual(len(report["files"]), 2)

    def test_truncated_picture_and_empty_resource_are_blocked(self):
        self.put("map/MAP00_1.bmp", b"BM")
        self.put("text/UI.txt", b"")
        self.assertEqual(self.scan()["summary"]["blocked"], 2)

    def test_reparse_or_link_is_not_followed(self):
        outside = self.base / "outside"; outside.mkdir()
        (outside / "asset.bmp").write_bytes(bmp())
        try:
            (self.source / "map").symlink_to(outside, target_is_directory=True)
        except OSError:
            self.skipTest("symlink creation unavailable; platform reparse guard remains in production")
        report = self.scan()
        self.assertEqual(report["summary"]["blocked"], 1)
        self.assertEqual(len(report["files"]), 1)

    def test_successful_import_preserves_paths_bytes_and_originals(self):
        self.put("PSX_DATA/map/MAP00_1.bmp")
        destination = self.base / "mods" / "demo"
        pc_mod.import_package(self.source, destination, self.catalog)
        self.assertEqual((destination / "PSX_DATA/map/MAP00_1.bmp").read_bytes(), bmp())
        self.assertEqual((self.source / "PSX_DATA/map/MAP00_1.bmp").read_bytes(), bmp())
        report = json.loads((destination / "compatibility-report.json").read_text())
        self.assertEqual(report["runtime_verified"], False)

    def test_blocked_package_is_never_partially_installed(self):
        self.put("map/MAP00_1.bmp")
        self.put("Message/missing.bmp")
        destination = self.base / "mods" / "demo"
        with self.assertRaises(pc_mod.PackageError):
            pc_mod.import_package(self.source, destination, self.catalog)
        self.assertFalse(destination.exists())

    def test_existing_destination_never_overwritten(self):
        self.put("map/MAP00_1.bmp")
        destination = self.base / "mods"; destination.mkdir()
        (destination / "keep").write_bytes(b"keep")
        with self.assertRaises(pc_mod.PackageError):
            pc_mod.import_package(self.source, destination, self.catalog)
        self.assertEqual((destination / "keep").read_bytes(), b"keep")

    def test_import_into_source_is_rejected(self):
        self.put("map/MAP00_1.bmp")
        with self.assertRaises(pc_mod.PackageError):
            pc_mod.import_package(self.source, self.source / "mods", self.catalog)
        self.assertFalse((self.source / "mods").exists())

    def test_known_loose_pc_and_packed_paths_are_read_from_production_tables(self):
        self.put("PSX_DATA/DOORNEW/DOOR_00/DOOR_00.HMD", b"changed")
        self.put("PSX_DATA/item/IO301_0/A.HMD", b"changed")
        self.assertEqual(self.scan()["summary"]["recognized"], 2)

    def test_resource_directory_can_be_imported_without_renaming_it(self):
        source = self.base / "Resource"; source.mkdir()
        (source / "TITLE10.bmp").write_bytes(bmp())
        report = pc_mod.inspect_package(source, self.catalog)
        self.assertEqual(report["summary"]["recognized"], 1)
        destination = self.base / "mod"
        pc_mod.import_package(source, destination, self.catalog)
        self.assertEqual((destination / "Resource/TITLE10.bmp").read_bytes(), bmp())

    def test_existing_native_resource_routes_remain_recognized(self):
        for name in ["BIGTIM/0.png", "DIARY/0/1.png", "NOISE/12/3.WAV", "NOISE/240/music1.seq", "M/OPENING.STR/42.png", "X/BGM_32.STR/5.wav"]:
            with self.subTest(name=name):
                self.assertIsNotNone(self.catalog.lookup(name.upper()))

    def test_bad_nac_command_reference_is_blocked(self):
        keys = 2
        self.put("nab/ENEM00.NAC", struct.pack("<I", keys) + bytes(112 * keys) + struct.pack("<I", 2) + bytes(8) + struct.pack("<II", 0, 7))
        self.assertEqual(self.scan()["files"][0]["status"], "blocked")

    def test_boss_nac_uses_its_final_key(self):
        for boss in [31, 32, 33]:
            self.put(f"nab/ENEM{boss}.NAC", struct.pack("<I", 2) + bytes(224) + struct.pack("<I", 2) + bytes(8) + struct.pack("<II", 0, 1))
        self.assertEqual(self.scan()["summary"]["recognized"], 3)

    def test_whole_archive_and_inner_asset_are_both_conflicts(self):
        self.put("B/MAPTIM.BIN", b"archive")
        self.put("map/MAP00_1.bmp")
        self.assertEqual(self.scan()["summary"]["blocked"], 2)

    def test_numeric_aliases_cannot_silently_overwrite_each_other(self):
        changed = bytearray(bmp()); changed[-1] = 37
        for first, second in [("BIGTIM/1.bmp", "BIGTIM/01.bmp"), ("DIARY/0/1.bmp", "DIARY/00/01.bmp"), ("FONT/FONT186.bmp", "FONT/FONT0186.bmp")]:
            with self.subTest(first=first):
                left = self.catalog.lookup(first.upper())
                right = self.catalog.lookup(second.upper())
                self.assertIsNotNone(right)
                self.assertEqual(left["targets"], right["targets"])
        self.put("BIGTIM/1.bmp"); self.put("BIGTIM/01.bmp", bytes(changed))
        self.assertEqual(self.scan()["summary"]["blocked"], 2)

    def test_raw_archives_overlap_native_conversion_routes(self):
        for archive, resource in [("OBJ.DAT", "NAB/ENEM00.NAB"), ("EV_OBJ.DAT", "FONT/FONT186.BMP"), ("B/BIGTIM.BIN", "TEXT/ITEMS.TXT")]:
            with self.subTest(archive=archive):
                self.assertIn(archive, self.catalog.lookup(resource)["archives"])

    def test_movie_frame_zero_has_no_runtime_route(self):
        self.assertIsNone(self.catalog.lookup("M/OPENING.STR/0.BMP"))

    def test_numeric_routes_respect_the_native_integer_domain(self):
        for name in ["M/OPENING.STR/4294967296.bmp", "X/BGM_32.STR/4294967296.wav", "BIGTIM/4294967295.bmp", "DIARY/4294967296/1.bmp", "NOISE/4294967296/1.wav"]:
            with self.subTest(name=name):
                self.assertIsNone(self.catalog.lookup(name.upper()))

    def test_executable_named_like_documentation_is_still_blocked(self):
        self.put("README.exe", b"MZ")
        self.assertEqual(self.scan()["summary"]["blocked"], 1)

    def test_verified_pc_sound_and_native_waveform_share_target(self):
        pc = self.catalog.lookup("WAV/ENEMY/DOG/SND-001.WAV")
        self.assertIsNotNone(pc)
        self.assertEqual(pc["targets"], self.catalog.lookup("NOISE/23/2.WAV")["targets"])
        self.assertIsNone(self.catalog.lookup("WAV/ENEMY/DOG/SND-000.WAV"))

    def test_pc_noise_preflight_accepts_only_valid_atomic_package(self):
        import pc_noise
        from pc_noise_test import bank
        self.catalog.noise_profile = pc_noise.build_profile(bank(1)+bank(2)+bank(3),bank(1)+bank(3),[0,4096,8192,12288],{1},set())
        self.put("PSX_DATA/noise/noise.bin",bank(1)+bank(3))
        report=self.scan()
        self.assertEqual(report["summary"]["recognized"],1)
        self.assertEqual(report["files"][0]["changed_banks"],[])
        changed=bytearray(bank(1)+bank(3));changed[4096+8]=0
        self.put("PSX_DATA/noise/noise.bin",changed)
        self.assertEqual(self.scan()["summary"]["blocked"],1)

    def test_pc_noise_and_individual_sound_conflict(self):
        import pc_noise
        from pc_noise_test import bank
        self.catalog.noise_profile = pc_noise.build_profile(bank(1),bank(1),[0,4096],set(),set())
        changed=bytearray(bank(1));changed[3120]=9
        self.put("PSX_DATA/noise/noise.bin",changed)
        self.put("NOISE/23/2.wav",wav())
        self.assertEqual(self.scan()["summary"]["blocked"],2)

    def test_unchanged_pc_noise_is_a_noop_beside_individual_sound(self):
        import pc_noise
        from pc_noise_test import bank
        self.catalog.noise_profile=pc_noise.build_profile(bank(1),bank(1),[0,4096],set(),set())
        self.put("PSX_DATA/noise/noise.bin",bank(1))
        self.put("NOISE/23/2.wav",wav())
        self.assertEqual(self.scan()["summary"]["recognized"],2)
        self.assertEqual(self.scan()["summary"]["blocked"],0)

    def test_import_checks_complete_pcm_chunks_before_runtime(self):
        for kind in ["fmt", "truncated", "empty"]:
            data=bytearray(wav())
            if kind=="fmt":struct.pack_into("<I",data,16,0)
            if kind=="truncated":struct.pack_into("<I",data,40,400)
            if kind=="empty":struct.pack_into("<I",data,40,0)
            self.put("NOISE/23/2.wav",data)
            with self.subTest(kind=kind):self.assertEqual(self.scan()["summary"]["blocked"],1)

    def test_unproven_dog_crow_events_are_not_automatically_assigned(self):
        for family,cue,block,vag in [("DOG",3,23,4),("DOG",5,23,12),("DOG",6,23,7),("DOG",8,23,9),("DOG",9,23,10),("CROW",2,22,4),("CROW",3,22,3),("CROW",4,22,5)]:
            with self.subTest(family=family,cue=cue):
                row=self.catalog.lookup(f"WAV/ENEMY/{family}/SND-{cue:03}.WAV")
                self.assertIsNone(row)
                self.assertIsNotNone(self.catalog.lookup(f"NOISE/{block}/{vag}.WAV"))
        self.assertIsNone(self.catalog.lookup("WAV/ENEMY/CROW/SND-006.WAV"))

    def test_verified_pickup_profiles_are_canvas_and_stream_guarded(self):
        for item,words,crc in [(1,31,'6702FD76'),(20,37,'B967BFF9'),(32,34,'BB38C67C'),(41,37,'9645E781'),(64,44,'C88BE6A5')]:
            row=self.catalog.lookup(f'ITEM/PIC/{item:02X}.BMP')
            self.assertEqual(row['targets'],[f'pickup:{item}'])
            self.assertEqual((row['native_stream_words'],row['native_stream_crc32']),(words,crc))
        self.put('PSX_DATA/item/PIC/01.bmp',bmp(4,4));self.assertEqual(self.scan()['summary']['blocked'],1)
    def test_complete_verified_pickup_catalog_and_explicit_exclusions(self):
        verified = (1, 2, 3, 4, 5, 8, 11, 12, 13, 14, 15, 20, 21, 22, 23, 32, 33, 34, 36, 37, 38, 39, 41, 43, 44, 45, 46, 47, 48, 52, 53, 54, 55, 64, 65, 66, 67, 68, 69, 71, 72, 73, 74, 75, 76)
        excluded = (6, 7, 9, 10, 16, 17, 18, 19, 35, 40, 42, 49, 50, 51, 70)
        for item in verified:
            with self.subTest(item=item):
                row = self.catalog.lookup(pc_mod.canonical_resource(f'PSX_DATA/item/PIC/{item:02X}.bmp'))
                self.assertEqual(row['targets'], [f'pickup:{item}'])
                self.assertGreater(row['native_stream_words'], 20)
        for item in excluded:
            self.assertIsNone(self.catalog.lookup(f'ITEM/PIC/{item:02X}.BMP'))

    def test_verified_message_profile_requires_its_pc_canvas(self):
        self.put("PSX_DATA/Message/0003-00.bmp",bmp())
        row=self.scan()["files"][0]
        self.assertEqual(row["status"],"blocked")
        self.assertIn("512",row["reason"])
        self.put("PSX_DATA/Message/0003-00.bmp",bmp(512,512))
        row=self.scan()["files"][0]
        self.assertEqual(row["status"],"recognized")
        self.assertEqual(row["targets"],["message:4:2"])
        self.assertEqual(row["native_stream_words"],68)
        self.assertIsNone(self.catalog.lookup("MESSAGE/0003-01.BMP"))

    def test_bad_relative_paths_are_rejected(self):
        for name in ["../map/a.bmp", "/map/a.bmp", "C:/a.bmp", "map//a.bmp", "map/./a.bmp", "map/CON.bmp", "x" * 512]:
            with self.subTest(name=name), self.assertRaises(pc_mod.PackageError):
                pc_mod.canonical_resource(name)


if __name__ == "__main__":
    unittest.main()
