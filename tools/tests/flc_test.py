"""Generated-data contract for the production bounded8-bit FLC decoder."""
from pathlib import Path
import struct,subprocess,tempfile,unittest,os
R=Path(__file__).resolve().parents[2]
def chunk(t,p):return struct.pack('<IH',6+len(p),t)+p
def frame(*chunks):
 p=b''.join(chunks);return struct.pack('<IHH8x',16+len(p),0xf1fa,len(chunks))+p
def flc(*frames,w=4,h=2,n=None,prefix=b''):
 hdr=bytearray(128);struct.pack_into('<I5H',hdr,0,128+len(prefix)+sum(map(len,frames)),0xaf12,len(frames)if n is None else n,w,h,8)
 return bytes(hdr)+prefix+b''.join(frames)
def pal():return chunk(4,struct.pack('<HBB',1,0,3)+bytes([0,0,0,255,0,0,0,255,0]))
class FlcTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  assert (R/'port/src/modfs_flc.h').exists(),'bounded FLC decoder absent'
  cls.tmp=tempfile.TemporaryDirectory();cls.root=Path(cls.tmp.name)
  code='''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "modfs_flc.h"
int main(int argc,char **argv){if(argc!=3)return 3;FILE *f=fopen(argv[1],"rb");fseek(f,0,2);long n=ftell(f);rewind(f);uint8_t *d=malloc(n);fread(d,1,n,f);fclose(f);uint8_t *p=modfs_flc_decode(d,n,4,2,2);free(d);if(!p)return 2;f=fopen(argv[2],"wb");fwrite(p,1,4*2*2*4,f);fclose(f);free(p);return 0;}
'''
  c=cls.root/'test.c';c.write_text(code);cls.exe=cls.root/'flc.exe'
  import shlex;cc=shlex.split(os.environ.get('CC','clang'),posix=os.name!='nt')
  subprocess.run([*cc,'-Wall','-Wextra','-Werror','-I'+str(R/'port/src'),str(c),'-o',str(cls.exe)],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def decode(self,data,valid=True):
  a=self.root/'in.flc';b=self.root/'out.rgba';a.write_bytes(data);p=subprocess.run([str(self.exe),str(a),str(b)])
  self.assertEqual(p.returncode,0 if valid else 2)
  return b.read_bytes()if valid else None
 def test_copy_palette_and_repeat_frame(self):
  d=self.decode(flc(frame(pal(),chunk(16,bytes([1,2,0,1,2,1,2,0]))),frame()))
  self.assertEqual(d[:12],bytes([255,0,0,255,0,255,0,255,0,0,0,0]));self.assertEqual(d[:32],d[32:])
 def test_brun_positive_repeat_negative_literal(self):
  d=self.decode(flc(frame(pal(),chunk(15,bytes([1,4,1,1,252,2,1,2,1]))),frame()))
  self.assertEqual(d[:16],bytes([255,0,0,255])*4);self.assertEqual(d[16:24],bytes([0,255,0,255,255,0,0,255]))
 def test_ss2_skip_last_pixel_and_word_runs(self):
  delta=struct.pack('<HHHHBB2B',1,0xffff,0x8002,1,0,255,2,1)
  d=self.decode(flc(frame(pal(),chunk(16,bytes([1]*8))),frame(chunk(7,delta))))
  self.assertEqual(d[32:48],d[:16]);self.assertEqual(d[48:56],bytes([0,255,0,255,255,0,0,255]));self.assertEqual(d[-4:],bytes([0,255,0,255]))
 def test_old_delta_and_palette64(self):
  palette=chunk(11,struct.pack('<HBB',1,0,3)+bytes([0,0,0,63,0,0,0,63,0]))
  d=self.decode(flc(frame(palette,chunk(13,b'')),frame(chunk(12,struct.pack('<HH',1,1)+bytes([1,0,252,1])))))
  self.assertEqual(d[48:],bytes([252,0,0,255])*4)
 def test_prefix_and_ring(self):
  ring=frame(chunk(13,b''));d=self.decode(flc(frame(pal(),chunk(16,bytes([1]*8))),frame(),ring,n=2,prefix=chunk(0xf100,b'\0'*10)))
  self.assertEqual(d,bytes([255,0,0,255])*16)
 def test_truncations_and_bad_dimensions(self):
  good=flc(frame(pal(),chunk(16,bytes([1]*8))),frame())
  for n in [0,3,127,128,len(good)-1]:self.decode(good[:n],False)
  self.decode(flc(frame(),frame(),w=0),False)
 def test_row_and_packet_overflows(self):
  for t,p in [(16,bytes(7)),(15,bytes([1,5,1])),(7,struct.pack('<HHBB',1,1,4,2)+bytes(4)),(12,struct.pack('<HH',2,1)+bytes([0])),(4,struct.pack('<HBB',1,255,2)+bytes(6)),(999,b'')]:
   self.decode(flc(frame(pal(),chunk(16,bytes([1]*8))),frame(chunk(t,p))),False)
if __name__=='__main__':unittest.main(verbosity=2)
