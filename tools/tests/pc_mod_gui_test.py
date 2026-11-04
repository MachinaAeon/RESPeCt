"""Asset-free tests for the desktop installer's real backend."""
from pathlib import Path
import sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import pc_mod_gui as gui
import pc_mod

class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.base=Path(self.tmp.name);self.game=self.base/'Game';self.game.mkdir()
        (self.game/'RESPeCt.exe').write_bytes(b'not executed')
        self.source=self.base/'Source';self.source.mkdir()
        self.catalog=pc_mod.Catalog(ROOT/'port/src')
    def text(self):
        p=self.source/'text/UI.txt';p.parent.mkdir();p.write_text('@RETURN\nBack\n',encoding='utf-8');return p
    def test_portable_catalog_is_located_relative_to_tool(self):
        self.assertTrue(gui.catalog().lookup('TEXT/UI.TXT'))
    def test_install_copies_recognized_files_without_changing_source_or_order(self):
        p=self.text();before=p.read_bytes();mods=self.game/'mods';mods.mkdir()
        (mods/'order.txt').write_text('previous\n');dest,report,notice=gui.install(self.source,self.game,'New mod',self.catalog)
        self.assertEqual(p.read_bytes(),before);self.assertEqual((dest/'text/UI.txt').read_bytes(),before)
        self.assertEqual((mods/'order.txt').read_text(),'previous\n');self.assertIn('order.txt',notice)
        self.assertEqual(report['summary']['blocked'],0);self.assertFalse(report['runtime_verified'])
    def test_invalid_name_cannot_escape_mods_or_be_silently_disabled(self):
        for name in ['../escape','a/b','a\\b','_disabled','CON','name.','x:y','', 'a\nname','#New',';New','CON.txt','COM1.foo','lpt1.txt']:
            with self.subTest(name=name),self.assertRaises(pc_mod.PackageError):gui.destination(self.game,name)
    def test_game_root_must_be_an_installation(self):
        with self.assertRaises(pc_mod.PackageError):gui.destination(self.source,'mod')
    def test_unknown_asset_prevents_all_copying(self):
        self.text();(self.source/'unknown.bin').write_bytes(b'?')
        with self.assertRaises(pc_mod.PackageError):gui.install(self.source,self.game,'mod',self.catalog)
        self.assertFalse((self.game/'mods/mod').exists())
    def test_existing_destination_is_never_overwritten(self):
        self.text();dest=self.game/'mods/mod';dest.mkdir(parents=True);(dest/'keep').write_text('original')
        with self.assertRaises(pc_mod.PackageError):gui.install(self.source,self.game,'mod',self.catalog)
        self.assertEqual((dest/'keep').read_text(),'original')
    def test_destination_cannot_be_inside_source(self):
        self.text();game=self.source/'game';game.mkdir();(game/'RESPeCt.exe').write_bytes(b'not executed')
        with self.assertRaises(pc_mod.PackageError):gui.install(self.source,game,'mod',self.catalog)
    def test_disabled_and_missing_order_entries_are_reported(self):
        self.text();dest,_,notice=gui.install(self.source,self.game,'active',self.catalog)
        self.assertIn('Restart',notice);self.assertTrue(dest.is_dir())
    def test_linked_mods_directory_is_rejected(self):
        self.text();other=self.base/'other';other.mkdir()
        try:(self.game/'mods').symlink_to(other,target_is_directory=True)
        except OSError:self.skipTest('symlink creation not permitted')
        with self.assertRaises(pc_mod.PackageError):gui.destination(self.game,'mod')

if __name__=='__main__':unittest.main()
