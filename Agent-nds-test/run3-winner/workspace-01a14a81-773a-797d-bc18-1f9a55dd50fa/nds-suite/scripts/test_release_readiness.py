#!/usr/bin/env python3
"""Check release gate fails closed; fixtures never authorize a real release."""
import json, shutil, subprocess, sys, tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[1]
checks={}
with tempfile.TemporaryDirectory() as temp:
 root=Path(temp)
 for name in ['scripts/release_gate.py','docs/remaining-cases.json','reports/development.json','reports/development-validation.json','coverage.json','release/v1.0-plan.json','suite.nds','LICENSE']:
  dest=root/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(R/name,dest)
 def run():
  p=subprocess.run([sys.executable,str(root/'scripts/release_gate.py')],capture_output=True,text=True,check=False)
  return p.returncode,json.loads(p.stdout)
 code,d=run();assert code==2 and d['status']=='BLOCKED'
 areas={x['area'] for x in d['blockers']}
 assert {'upstream_redistribution','release_regression','v1_verified_standalone_folder'}<=areas
 assert 'original_code_license' not in areas
 checks['current_release_blockers_enforced']='PASS'
 plan=root/'release/v1.0-plan.json';obj=json.loads(plan.read_text());obj['license_approved']=True;plan.write_text(json.dumps(obj))
 (root/'LICENSE').unlink()
 code,d=run();assert code==2 and any(x['area']=='original_code_license' for x in d['blockers'])
 checks['approval_without_license_file_remains_blocked']='PASS'
 plan.unlink();code,d=run();assert code==3 and d['status']=='ERROR'
 checks['missing_release_plan_fails_closed']='PASS'
report={'scope':'Release gate regression only; not coverage or legal clearance','tests':checks}
(R/'reports/release-readiness-tests.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
