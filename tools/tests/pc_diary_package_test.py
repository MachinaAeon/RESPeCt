"""Complete PC diary package checks, generated images only."""
from pathlib import Path
import sys, unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
import pc_mod_test as fixtures
bmp=fixtures.bmp
import pc_mod
class DiaryPackages(unittest.TestCase):
    setUp=fixtures.PcModTests.setUp
    put=fixtures.PcModTests.put
    scan=fixtures.PcModTests.scan
    def book(self,number=1,pages=4,backgrounds=0,wrapper='PSX_DATA/'):
        for k in range(pages):self.put(f'{wrapper}diary/file{number:02x}/file{number:02x}_{k:x}.bmp',bmp(512,512))
        for k in range(backgrounds):self.put(f'{wrapper}diary/file{number:02x}/file{number:02x}_b'+(''if backgrounds==1 else f'{k:x}')+'.bmp',bmp(512,448))
    def test_title_is_independent_and_uses_decimal_mapping(self):
        self.put('PSX_DATA/diary/D-NAME00.bmp',bmp(640,480));self.put('PSX_DATA/diary/D-NAME12.bmp',bmp(640,480))
        r=self.scan();self.assertEqual(r['summary']['recognized'],2);self.assertEqual(r['summary']['blocked'],0)
        self.assertEqual([x['targets'] for x in r['files']],[['pc-diary-title:1'],['pc-diary-title:12']])
    def test_title_requires_full_canvas_not_page_dimensions(self):
        self.put('diary/D-NAME00.bmp',bmp(512,512));self.assertEqual(self.scan()['summary']['blocked'],1)
    def test_other_ui_and_hex_title_names_remain_blocked(self):
        for name in ['D-NAME0C.bmp']:self.put('diary/'+name)
        self.assertEqual(self.scan()['summary']['blocked'],1)
    def test_keys_are_an_independent_validated_atlas(self):
        self.put('PSX_DATA/diary/KEY-AB.bmp',bmp(320,240))
        r=self.scan();self.assertEqual(r['summary']['recognized'],1);self.assertEqual(r['summary']['blocked'],0)
        self.assertEqual(r['files'][0]['targets'],['pc-diary-keys'])
    def test_keys_reject_wrong_canvas(self):
        self.put('diary/KEY-AB.bmp',bmp(640,480))
        self.assertEqual(self.scan()['summary']['blocked'],1)
    def test_book_flc_is_independent_with_exact_layout(self):
        import struct
        d=bytearray(128+18*16);struct.pack_into('<I5H',d,0,len(d),0xaf12,18,196,82,8)
        for i in range(18):struct.pack_into('<IHH',d,128+i*16,16,0xf1fa,0)
        self.put('diary/BOOK.FLC',d);r=self.scan()
        self.assertEqual(r['summary']['recognized'],1);self.assertEqual(r['summary']['blocked'],0)
        self.assertEqual(r['files'][0]['targets'],['pc-diary-book'])
        struct.pack_into('<H',d,8,195);self.put('diary/BOOK.FLC',d)
        self.assertEqual(self.scan()['summary']['blocked'],1)
    def test_complete_pc_diary_is_automatic(self):
        self.book();r=self.scan();self.assertEqual(r['summary']['recognized'],4);self.assertEqual(r['summary']['blocked'],0)
        self.assertTrue(all(x['kind']=='pc-diary' and x['diary_native']==0 for x in r['files']))
    def test_missing_companion_blocks_whole_document(self):
        self.book(pages=3);r=self.scan();self.assertEqual(r['summary']['recognized'],0);self.assertEqual(r['summary']['blocked'],3)
        self.assertTrue(all('complete' in x['reason'] for x in r['files']))
    def test_invalid_page_blocks_other_companions(self):
        self.book();self.put('PSX_DATA/diary/file01/file01_2.bmp',bmp(4,4));r=self.scan();self.assertEqual(r['summary']['recognized'],0);self.assertEqual(r['summary']['blocked'],4)
    def test_measured_multi_background_set(self):
        self.book(12,6,3,wrapper='');r=self.scan();self.assertEqual(r['summary']['recognized'],9);self.assertEqual(r['summary']['blocked'],0)
        self.assertTrue(all(x['diary_native']==12 for x in r['files']))
    def test_foreground_and_background_dimensions_are_distinct(self):
        self.book(2,5,1);self.put('PSX_DATA/diary/file02/file02_b.bmp',bmp(512,512));r=self.scan();self.assertEqual(r['summary']['recognized'],0);self.assertEqual(r['summary']['blocked'],6)
    def test_missing_background_is_not_silently_omitted(self):
        self.book(2,5);self.assertEqual(self.scan()['summary']['blocked'],5)
    def test_extra_page_or_wrong_document_prefix_is_blocked(self):
        self.book();self.put('PSX_DATA/diary/file01/file01_4.bmp');self.put('PSX_DATA/diary/file01/file02_1.bmp');self.assertEqual(self.scan()['summary']['blocked'],2)
    def test_oversized_encoded_image_is_blocked_before_runtime(self):
        self.book();self.put('PSX_DATA/diary/file01/file01_2.bmp',bmp(512,512)+bytes(8*1024*1024));r=self.scan()
        self.assertEqual(r['summary']['recognized'],0);self.assertEqual(r['summary']['blocked'],4)
        self.assertTrue(any('8 MiB' in x['reason'] for x in r['files']))
    def test_complete_import_preserves_sources(self):
        self.book();before={str(p):p.read_bytes() for p in self.source.rglob('*.bmp')};dest=self.base/'installed';pc_mod.import_package(self.source,dest,self.catalog)
        self.assertTrue((dest/'PSX_DATA/diary/file01/file01_0.bmp').is_file());self.assertEqual(before,{str(p):p.read_bytes() for p in self.source.rglob('*.bmp')})
if __name__=='__main__':unittest.main()
