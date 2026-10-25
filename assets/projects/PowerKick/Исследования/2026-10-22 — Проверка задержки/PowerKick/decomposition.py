from pathlib import Path
import subprocess,array,math,json
p=Path(__file__).resolve().parent;b=Path.home()/'Library/Caches/PowerKickBuildDev/powerkick_tests_artefacts/Release/powerkick_tests';presets={}
for line in (p/'preset-dump.log').read_text().splitlines():
 name,cat,vals=line.split('|');presets[name]={k:float(v) for k,v in (x.split('=') for x in vals.split())}
err=(p/'decomposition.stderr.log').open('w');proc=subprocess.Popen([str(b),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err)
def render(vals,step,vel,alt,bpm):
 vals={**vals,'output':vals['output']-24,'_len':1,'_sr':48000,'_bpm':bpm,'_vel':vel,'_step':step,'_alt':alt};line=' '.join(f'{k}={v:.9g}' for k,v in vals.items())+'\n';proc.stdin.write(line.encode());proc.stdin.flush();buf=bytearray();n=48000*8
 while len(buf)<n:
  chunk=proc.stdout.read(n-len(buf))
  if not chunk:raise RuntimeError('short read')
  buf+=chunk
 a=array.array('f');a.frombytes(buf);v=[a[::2],a[1::2],[(a[i]+a[i+1])*.5 for i in range(0,len(a),2)],[(a[i]-a[i+1])*.5 for i in range(0,len(a),2)]];peaks=[]
 for x in v:
  j=max(range(len(x)),key=lambda i:abs(x[i]));peaks.append(dict(dbfs=20*math.log10(max(abs(x[j]),1e-15))+24,sample=j,hit=j//step if step else 0,offset_ms=(j%step if step else j)/48))
 return dict(zip(['left','right','mid','side'],peaks))
rows=[]
variants={'original':{},'no_rumble':{'rumble':0},'mono':{'width':0},'no_character':{'character':0},'no_drive':{'drive':0},'no_click':{'click':0},'no_body_scoop':{'bodyTone':1},'bare_body':{'character':0,'drive':0,'rumble':0,'click':0,'width':0,'bodyTone':1},'no_character_mono':{'character':0,'width':0},'no_character_no_body_scoop':{'character':0,'bodyTone':1}}
for name,bpm in [('Warm Click F#1 III',210),('Classic Punch',210),('Crushed Click A#1',150)]:
 for label,change in variants.items():
  vals={**presets[name],**change}
  for label2,step,vel,alt in [('single_1',0,1,0),('single_025',0,.25,0),('roll_full',1714 if bpm==210 else 2400,1,0),('roll_accent',1714 if bpm==210 else 2400,.8,1)]:
   out=render(vals,step,vel,alt,bpm);rows.append(dict(preset=name,variant=label,case=label2,**out))
proc.stdin.close();code=proc.wait();err.close();(p/'decomposition.json').write_text(json.dumps(dict(returncode=code,rows=rows),ensure_ascii=False,indent=2))
for name in ['Warm Click F#1 III','Classic Punch','Crushed Click A#1']:
 print('\n'+name)
 for variant in variants:
  rs=[r for r in rows if r['preset']==name and r['variant']==variant];print(variant,[(r['case'],round(max(r['left']['dbfs'],r['right']['dbfs']),3)) for r in rs])
