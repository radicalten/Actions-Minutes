#!/usr/bin/env python3
import json,struct,unittest
from scoring import decode_score,aggregate
from pathlib import Path
R=Path(__file__).resolve().parents[1]
class Tests(unittest.TestCase):
 def test_vectors(self):
  rows=json.loads((R/'src/generated/vectors.json').read_text())
  self.assertEqual(len(rows),3390)
  self.assertEqual([v['id'] for v in rows],list(range(1,3391)))
  self.assertEqual(sum(v.get('source')=='mgba-suite-auto/carry.c' for v in rows),93)
  self.assertEqual(len({v['name'] for v in rows}),260)
 def test_groups(self):
  g=json.loads((R/'src/groups.json').read_text());self.assertEqual(len(g),25)
  self.assertEqual([r['id'] for r in g],list(range(25)))
 def test_rom_layout(self):
  rom=(R/'suite.nds').read_bytes()
  for pos,addr in [(0x20,0x02000100),(0x30,0x02200000)]:
   off,entry,ram,size=struct.unpack_from('<IIII',rom,pos)
   self.assertEqual(entry,addr);self.assertEqual(ram,addr)
   self.assertGreater(size,0);self.assertLessEqual(off+size,len(rom))
   self.assertLess(size,0x100000 if pos==0x30 else 0x1eff00)
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
  self.assertEqual(q['vectors'],3390)
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
 def test_score_completed(self):
  r=json.loads((R/'reports/cpu9.json').read_text())['results'][0]
  self.assertEqual(r['score']['percent'],0)
  self.assertEqual(r['score']['cpu9'],{'passed':6178,'failed':320})
  self.assertTrue(r['score']['complete'])
 def test_resumed_score(self):
  report=json.loads((R/'reports/resilient.json').read_text());r=report['results'][0]
  score=decode_score(r['last_mailbox'])
  self.assertEqual(score['passed']+score['failed']+score['unrun'],score['planned'])
  self.assertEqual(score['unrun'],0);self.assertTrue(score['complete'])
  self.assertEqual(score['percent'],score['passed']*10000//score['planned']/100)
  self.assertEqual([s['group'] for s in report['segments'] if s['outcome']=='EMULATOR_CRASH'],[20,21])
  self.assertEqual(sum(g['status']=='EMULATOR_CRASH' for g in report['groups']),2)
 def test_score_invalid_mailbox(self):
  r=json.loads((R/'reports/cpu9.json').read_text())['results'][0]
  w=r['last_mailbox'][:];w[10]=9999
  with self.assertRaises(ValueError):decode_score(w)
 def test_crash_denominator(self):
  s=aggregate([{'status':'PASS'},{'status':'EMULATOR_CRASH'}])
  self.assertEqual(s['percent'],50);self.assertEqual(s['crashed'],1)
  self.assertFalse(s['all_groups_resolved'])
 def test_qemu_rock(self):
  r=json.loads((R/'reports/qemu-rock.json').read_text())
  self.assertEqual(len(r['results']),11)
  self.assertTrue(all(x['status']=='PASS' for x in r['results']))
 def test_cpu_failure_recovery(self):
  r=json.loads((R/'reports/negative-recovery.json').read_text())['results'][0]
  self.assertEqual(r['score']['cpu9'],{'passed':6177,'failed':321})
  self.assertGreater(r['last_mailbox'][3],0)
 def test_thumb_condition_matrix(self):
  rows=json.loads((R/'src/generated/vectors.json').read_text())
  cases=[v for v in rows if v['name'].startswith('cpu_thumb_branch_')]
  self.assertEqual(len(cases),224)
  self.assertEqual(len({v['name'] for v in cases}),14)
  for name in {v['name'] for v in cases}:
   self.assertEqual({v['initial'] for v in cases if v['name']==name},{f<<28 for f in range(16)})
 def test_both_lcds_and_navigation(self):
  r=json.loads((R/'reports/ui-tests.json').read_text())
  self.assertEqual(len(r['results']),12)
  self.assertTrue(r['both_lcds_verified']);self.assertTrue(r['score_preserved'])
  self.assertTrue(all(x['status']=='PASS' for x in r['results']))
 def test_new_thumb_negative_checks(self):
  for name in ['branch_eq','push_lr','pop_pc']:
   r=json.loads((R/('reports/negative-thumb-'+name+'.json')).read_text())
   self.assertEqual(r['status'],'FAIL')
   self.assertGreater(r['assertion'],1181)
   self.assertEqual(r['score']['cpu9'],{'passed':6177,'failed':321})
 def test_returned_failure_and_hang_recovery(self):
  r=json.loads((R/'reports/recovery-tests.json').read_text())
  self.assertTrue(all(status=='PASS' for status in r.values()))
  cp=json.loads((R/'reports/resilient/checkpoint-00.json').read_text())
  self.assertEqual(cp['ledger'][30],2) # IPCFIFO failed
  self.assertEqual(cp['ledger'][32],1) # IPCFIFO IRQ ran and passed afterwards
  self.assertEqual(cp['active'],20) # no host restart was needed before the native crash
 def test_arm_conditional_matrix(self):
  rows=json.loads((R/'src/generated/vectors.json').read_text())
  cases=[v for v in rows if v['name'].startswith('cpu_arm_cond_')]
  self.assertEqual(len(cases),240);self.assertEqual(len({v['name'] for v in cases}),15)
 def test_new_arm_fault_injection(self):
  for name in ['condition','operand2','psr']:
   r=json.loads((R/('reports/negative-arm-'+name+'.json')).read_text())
   self.assertEqual(r['status'],'FAIL');self.assertGreater(r['assertion'],1534)
   self.assertEqual(r['score']['cpu9'],{'passed':6177,'failed':321})
 def test_arm9_only_vectors(self):
  m=json.loads((R/'src/generated/debug-manifest.json').read_text())
  self.assertEqual((m['shared_count'],m['arm9_count'],m['arm7_count']),(3390,6498,5174))
  self.assertEqual([v['id'] for v in m['vectors']],list(range(1,6499)))
  self.assertEqual(json.loads((R/'reports/qemu-v5.json').read_text())['status'],'PASS')
 def test_diagnostic_integration(self):
  r=json.loads((R/'reports/diagnostic-tests.json').read_text())
  self.assertEqual(len(r),5);self.assertTrue(all(v=='PASS' for v in r.values()))
 def test_complete_trace_and_journal(self):
  from diagnostics import cpu_rows,assertion_rows
  r=json.loads((R/'reports/resilient.json').read_text())
  for cpu,total in [('arm9',6498),('arm7',5174)]:
   rows=cpu_rows(r['diagnostics'][cpu]);self.assertEqual(len(rows),total)
   self.assertTrue(all(v['historical'] for v in rows))
   self.assertEqual(sum(v['status']=='FAIL' for v in rows),23 if cpu=='arm7' else 320)
  self.assertEqual(len(assertion_rows(r['diagnostics']['assertions'])),160)
  self.assertEqual(len(json.loads((R/'src/generated/assertions.json').read_text())['instrumented']),117)
 def test_trace_corruption_rejected(self):
  import tempfile
  from diagnostics import cpu_rows
  r=json.loads((R/'reports/resilient.json').read_text())['diagnostics']['arm9']
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'bad.bin';p.write_bytes(b'bad')
   with self.assertRaises(ValueError):cpu_rows(dict(r,path=str(p)))
 def test_arm7_specific_execution(self):
  r=json.loads((R/'reports/arm7-specific-tests.json').read_text())
  self.assertTrue(all(v=='PASS' for v in r['tests'].values()))
  self.assertEqual(r['specific_vectors'],1784)
  self.assertEqual(r['reference_mismatches'],23)
 def test_cpu_qualified_vector_ids(self):
  m=json.loads((R/'src/generated/debug-manifest.json').read_text())
  self.assertEqual(m['schema'],2)
  self.assertEqual([v['id'] for v in m['arm7_vectors']],list(range(1,5175)))
  self.assertNotEqual(m['arm7_vectors'][3390]['name'],m['vectors'][3390]['name'])
 def test_privileged_and_irq_evidence(self):
  r=json.loads((R/'reports/privileged-tests.json').read_text())
  self.assertTrue(all(v=='PASS' for v in r['tests'].values()))
  self.assertEqual(r['new_vectors'],1052)
 def test_privileged_qemu_oracle(self):
  r=json.loads((R/'reports/qemu-privileged.json').read_text())
  self.assertEqual(r['vectors'],956)
  self.assertEqual(r['exact_arm7tdmi'],'UNAVAILABLE')
  self.assertTrue(all(x['status']=='PASS' for x in r['results']))
 def test_milestone8_regression(self):
  r=json.loads((R/'reports/milestone8-tests.json').read_text())
  self.assertEqual(len(r['tests']),8)
  self.assertTrue(all(x=='PASS' for x in r['tests'].values()))
  self.assertEqual(r['primary_injected_helpers'],220)
 def test_cp15_alias_oracle(self):
  r=json.loads((R/'reports/qemu-cp15.json').read_text())
  self.assertEqual((r['vectors'],r['status']),(208,'PASS'))
 def test_raw_oracle_scope(self):
  r=json.loads((R/'reports/qemu-exceptions.json').read_text())
  self.assertEqual((r['status'],r['passed'],r['unsupported']),('PARTIAL_UNSUPPORTED',128,128))
  self.assertEqual(r['mpu_enable_readback'],'ZERO')
  self.assertTrue(all(v['aux']&512 for v in r['mismatches']))
 def test_trace_capacity_and_scratch_guard(self):
  m=json.loads((R/'src/generated/debug-manifest.json').read_text())
  self.assertLess(m['arm9_count'],6552);self.assertLess(m['arm7_count'],6552)
  self.assertIn('LENGTH = 0x1EFF00',(R/'src/linker/arm9.ld').read_text())
  self.assertIn('< 0x1eff00',(R/'scripts/build.sh').read_text())
 def test_operand2_execution_regression(self):
  r=json.loads((R/'reports/operand2-tests.json').read_text())
  self.assertEqual(r['cases_per_cpu'],256)
  self.assertEqual(len(r['tests']),9)
  self.assertTrue(all(v=='PASS' for v in r['tests'].values()))
 def test_operand2_independent_oracle(self):
  r=json.loads((R/'reports/qemu-operand2.json').read_text())
  self.assertEqual(r['vectors'],256)
  self.assertTrue(all(v['status']=='PASS' for v in r['results']))
  self.assertEqual(r['exact_arm7tdmi'],'UNAVAILABLE')
 def test_compare2_execution_regression(self):
  r=json.loads((R/'reports/compare2-tests.json').read_text())
  self.assertEqual(r['cases_per_cpu'],256)
  self.assertEqual(len(r['tests']),9)
  self.assertTrue(all(v=='PASS' for v in r['tests'].values()))
 def test_compare2_independent_oracle(self):
  r=json.loads((R/'reports/qemu-compare2.json').read_text())
  self.assertEqual(r['vectors'],256)
  self.assertTrue(all(v['status']=='PASS' for v in r['results']))
  self.assertEqual(r['exact_arm7tdmi'],'UNAVAILABLE')
if __name__=='__main__':unittest.main()
