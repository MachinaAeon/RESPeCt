"""Asset-free ordered-PC-noise profile and preflight tests."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pc_noise


def bank(value=1):
    b=bytearray(4096)
    struct.pack_into("<III",b,0,2,12,3120)
    b[12:16]=b"pBAV"
    struct.pack_into("<I",b,16,7)
    struct.pack_into("<H",b,12+18,1)
    struct.pack_into("<H",b,12+22,1)
    struct.pack_into("<H",b,12+32+2048+512+2,4)
    b[3120:3152]=bytes([value])*32
    return bytes(b)


class NoiseTests(unittest.TestCase):
    def setUp(self):
        self.native=bank(1)+bank(2)+bank(3)
        self.pc=bank(1)+bank(3)
        self.profile=pc_noise.build_profile(self.native,self.pc,[0,4096,8192,12288],{1},set())

    def test_order_is_preserved_for_identical_banks(self):
        profile=pc_noise.build_profile(bank(1)*3,bank(1)*2,[0,4096,8192,12288],{1},set())
        self.assertEqual([(r["native_offset"],r["pc_offset"]) for r in profile["parts"]],[(0,0),(8192,4096)])

    def test_unchanged_reference_has_no_native_changes(self):
        self.assertEqual(pc_noise.preflight(self.pc,self.profile),[])

    def test_valid_modified_sample_has_one_ordered_target(self):
        changed=bytearray(self.pc);changed[4096+3120]=9
        rows=pc_noise.preflight(changed,self.profile)
        self.assertEqual([r["native_offset"] for r in rows],[8192])

    def test_baseline_difference_requires_explicit_evidence(self):
        with self.assertRaises(ValueError):
            pc_noise.build_profile(self.native,bank(4)+bank(3),[0,4096,8192,12288],{1},set())

    def test_original_pc_difference_is_retained_but_modified_version_rejected(self):
        profile=pc_noise.build_profile(self.native,bank(4)+bank(3),[0,4096,8192,12288],{1},{0})
        self.assertEqual(pc_noise.preflight(bank(4)+bank(3),profile),[])
        changed=bytearray(bank(4)+bank(3));changed[3120]=8
        with self.assertRaisesRegex(ValueError,"block 0"):
            pc_noise.preflight(changed,profile)

    def test_truncated_and_enlarged_archives_rejected(self):
        for data in [self.pc[:-1],self.pc+b"x"]:
            with self.subTest(size=len(data)),self.assertRaises(ValueError):
                pc_noise.preflight(data,self.profile)

    def test_changed_part_offsets_rejected(self):
        changed=bytearray(self.pc);struct.pack_into("<I",changed,8,3124)
        with self.assertRaises(ValueError):pc_noise.preflight(changed,self.profile)

    def test_larger_spu_allocation_rejected(self):
        changed=bytearray(self.pc);struct.pack_into("<H",changed,12+32+2048+512+2,8)
        with self.assertRaises(ValueError):pc_noise.preflight(changed,self.profile)

    def test_incomplete_or_overlapping_profile_rejected(self):
        for mutate in [lambda r:r.update(pc_offset=4),lambda r:r.update(native_offset=0)]:
            profile=pc_noise.build_profile(self.native,self.pc,[0,4096,8192,12288],{1},set())
            mutate(profile["parts"][1])
            with self.assertRaises(ValueError):pc_noise.preflight(self.pc,profile)

    def test_reserved_waveform_size_cannot_shift_spu_addresses(self):
        for reserved, size in [(65535,4),(1,3)]:
            changed=bytearray(self.pc)
            struct.pack_into("<H",changed,12+32+2048+512,reserved)
            struct.pack_into("<H",changed,12+32+2048+512+2,size)
            with self.subTest(reserved=reserved),self.assertRaises(ValueError):
                pc_noise.preflight(changed,self.profile)

    def test_vab_structural_header_changes_are_rejected(self):
        for offset, value in [(16,4),(24,65535)]:
            changed=bytearray(self.pc);struct.pack_into("<I",changed,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):pc_noise.preflight(changed,self.profile)

    def test_header_roundtrip_contains_only_metadata(self):
        text=pc_noise.header(self.profile)
        parsed=pc_noise.read_profile_text(text)
        self.assertEqual(parsed,self.profile)
        self.assertNotIn("pBAV",text)

if __name__=="__main__":unittest.main()
