"""Score units are registered groups, not weighted sub-assertions or CPU vectors."""
from collections import Counter

def decode_score(words):
 if len(words)<18 or words[0]!=0x4e445354 or words[1]!=3:
  raise ValueError('Unknown mailbox protocol')
 passed,planned,failed,unrun,basis=words[5],words[7],words[8],words[9],words[10]
 if not planned or passed+failed+unrun!=planned or basis!=passed*10000//planned:
  raise ValueError('Inconsistent score counters')
 if words[11]!=int(unrun==0):raise ValueError('Invalid completion flag')
 return dict(unit='test_groups',passed=passed,failed=failed,planned=planned,unrun=unrun,
   percent=basis/100,complete=not unrun,
   cpu9={'passed':words[14],'failed':words[15]},cpu7={'passed':words[16],'failed':words[17]})

def aggregate(results):
 counts=Counter(r['status'] for r in results);n=len(results);passed=counts['PASS']
 return dict(unit='test_groups',passed=passed,planned=n,failed=counts['FAIL'],
  crashed=counts['EMULATOR_CRASH'],timeouts=sum(counts[s] for s in ('ROM_TIMEOUT','HOST_FRAME_TIMEOUT','HOST_WALL_TIMEOUT')),
  errors=sum(counts[s] for s in ('HARNESS_ERROR','PROTOCOL_ERROR')),
  percent=(passed*10000//n)/100 if n else 0,
  all_groups_attempted=bool(n),all_groups_resolved=bool(n) and passed+counts['FAIL']==n)
