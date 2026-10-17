#!/usr/bin/env python3
"""Read-only coverage-first readiness gate. JSON stdout. Exit0 READY,2 BLOCKED,3 ERROR."""
import argparse,hashlib,json,sys
from pathlib import Path
R=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--report',default='reports/development.json');p.add_argument('--output',default='reports/release-readiness.json');a=p.parse_args()
 try:
  ledger=json.loads((R/'docs/remaining-cases.json').read_text());r=json.loads((R/a.report).read_text());sha=hashlib.sha256((R/'suite.nds').read_bytes()).hexdigest()
  blockers=[{'area':x['area'],'status':x['status'],'scope':x['scope']} for x in ledger['items'] if x['status'] not in ('IMPLEMENTED_DEFINED_MATRIX','IMPLEMENTED_AND_VALIDATED')]
  # Defined matrices alone never authorize an exhaustive coverage claim.
  coverage=json.loads((R/'coverage.json').read_text())
  plan=json.loads((R/'release/v1.0-plan.json').read_text())
  if not plan.get('license_approved') or not (R/'LICENSE').is_file():blockers.append({'area':'original_code_license','status':'NOT_APPROVED','scope':'Owner approval and root license required; recommendation in docs/v1.0-release.md'})
  if not plan.get('redistribution_cleared'):blockers.append({'area':'upstream_redistribution','status':'BLOCKED','scope':'Pinned Rockwrestler snapshot lacks an explicit license; source/assets/ROM permission unresolved'})
  for milestone in plan['milestones']:
   if milestone['status']!='PASS':blockers.append({'area':'v1_'+milestone['id'],'status':milestone['status']})
  for gap in coverage['cpu']['uncovered']:blockers.append({'area':'cpu_uncovered','status':'OPEN','scope':gap})
  if any(r.get(key)!=expected for key,expected in {'group_schema':3,'score_protocol':3,'trace_protocol':4}.items()):blockers.append({'area':'protocol','status':'STALE_SCHEMA'})
  if r.get('base_rom_sha256')!=sha:blockers.append({'area':'evidence','status':'STALE_ROM_HASH'})
  if r.get('score',{}).get('unrun',1):blockers.append({'area':'execution','status':'UNRESOLVED_GROUPS'})
  validation=json.loads((R/'reports/development-validation.json').read_text())
  if validation.get('rom_sha256')!=sha or validation.get('full_release_regression')!='PASS':blockers.append({'area':'release_regression','status':'NOT_COMPLETE','scope':'Full current-ROM release regression/package refresh required; see development-validation.json'})
  result={'schema':1,'status':'BLOCKED' if blockers else 'READY','policy':'coverage_first','rom_sha256':sha,'report':a.report,'score':r.get('score'),'blockers':blockers,'note':'Known emulator test failures are results, not evidence of missing suite coverage. READY would not mean all emulators pass.'}
  code=2 if blockers else 0
 except Exception as e:result={'schema':1,'status':'ERROR','error':str(e)};code=3
 (R/a.output).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));return code
if __name__=='__main__':sys.exit(main())
