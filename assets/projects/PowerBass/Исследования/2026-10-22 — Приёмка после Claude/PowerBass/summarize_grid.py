from pathlib import Path
import csv,json,hashlib,itertools,subprocess
r=Path(__file__).parent;p=r.parents[2]
rows=list(csv.DictReader((r/'plan-grid-grid.csv').open()))
assert len(rows)==7200
expected=set(itertools.product([90,150,210],[16,32],[1.,.7],[0,1]))
by={}
for x in rows:
 key=(int(x['bpm']),int(x['division']),float(x['velocity']),int(x['gate_pattern']))
 by.setdefault(x['preset'],set()).add(key)
assert len(by)==300 and all(v==expected for v in by.values())
bad=[x for x in rows if int(x['safety_samples'])>0]
summary={'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=p,text=True).strip(),'acceptance':{'status':'FAIL' if bad else 'PASS','criterion':'0 safety activations on the required repeat grid','expected_cases':7200},'cases':len(rows),'presets':len(by),'cases_with_safety':len(bad),'presets_with_safety':len(set(x['preset'] for x in bad)),'safety_samples':sum(int(x['safety_samples']) for x in bad),'cases_with_safety_by_pattern':{str(k):sum(int(x['gate_pattern'])==k for x in bad) for k in [0,1]},'worst':sorted(bad,key=lambda x:float(x['pre_peak_dbfs']),reverse=True)[:12],'sample_rate':48000,'block':512,'seconds':3,'root_midi':36,'gate':.75,'offkick_pattern':{'0':'every step','1':'skip step divisible by 4'},'coverage':'300 presets x3BPM x2divisions x2velocities x2gate patterns','evidence':'external instrumented production engine; counts exact pk>.9 branch before unchanged safety; no change to production source','limitations':'finite C2 grid, not all roots/sample rates; not audition or DAW; a safety activation alone does not prove audible fault or identify engine cause','sha256':{str(f.relative_to(p)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [p/'Source/DSP/BassEngine.h',p/'Source/FactoryPresets.inc',p/'Source/Params.cpp',p/'Source/Presets.cpp',r/'BassEngineAudit.h',r/'bass_plan_grid.cpp',r/'bass_plan_grid',r/'plan-grid-grid.csv']}}
(r/'plan-grid-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k in ['cases','presets','cases_with_safety','presets_with_safety','safety_samples','cases_with_safety_by_pattern','worst']},ensure_ascii=False,indent=2))

raise SystemExit(1 if bad else 0)
