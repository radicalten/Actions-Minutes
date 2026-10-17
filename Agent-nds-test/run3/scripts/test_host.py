#!/usr/bin/env python3
import json,struct,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]
class Tests(unittest.TestCase):
 def test_vectors(self):
  rows=json.loads((R/'src/generated/vectors.json').read_text())
  self.assertEqual(len(rows),653)
  self.assertEqual([v['id'] for v in rows],list(range(1,654)))
  self.assertEqual(sum('source' in v for v in rows),93)
  self.assertEqual(len({v['name'] for v in rows}),35)
 def test_groups(self):
  g=json.loads((R/'src/groups.json').read_text());self.assertEqual(len(g),25)
  self.assertEqual([r['id'] for r in g],list(range(25)))
 def test_rom_layout(self):
  rom=(R/'suite.nds').read_bytes()
  for pos,addr in [(0x20,0x02000100),(0x30,0x03800100)]:
   off,entry,ram,size=struct.unpack_from('<IIII',rom,pos)
   self.assertEqual(entry,addr);self.assertEqual(ram,addr)
   self.assertGreater(size,0);self.assertLessEqual(off+size,len(rom))
   self.assertLess(size,0xef00 if pos==0x30 else 0x300000)
  self.assertEqual(rom.count(struct.pack('<II',0x53454c31,0x4e445341)),1)
 def test_header_crc(self):
  rom=(R/'suite.nds').read_bytes();crc=0xffff
  for b in rom[:0x15e]:
   crc^=b
   for _ in range(8):crc=(crc>>1)^(0xa001 if crc&1 else 0)
  self.assertEqual(crc,struct.unpack_from('<H',rom,0x15e)[0])
 def test_oracle_report(self):
  q=json.loads((R/'reports/qemu.json').read_text())
  self.assertEqual(q['exact_arm7tdmi'],'UNAVAILABLE')
  self.assertEqual(q['vectors'],653)
  self.assertTrue(all(r['status']=='PASS' for r in q['results']))
 def test_negative_selector(self):
  r=json.loads((R/'reports/negative-selector.json').read_text())
  self.assertEqual(r['status'],'FAIL');self.assertEqual(r['assertion'],0xffffffff)
 def test_negative_timeout(self):
  r=json.loads((R/'reports/negative-frame.json').read_text())
  self.assertEqual(r['status'],'HOST_FRAME_TIMEOUT')
 def test_injected_vector_failure(self):
  r=json.loads((R/'reports/negative-vector.json').read_text())
  self.assertEqual(r['status'],'FAIL');self.assertEqual(r['assertion'],1)
if __name__=='__main__':unittest.main()
