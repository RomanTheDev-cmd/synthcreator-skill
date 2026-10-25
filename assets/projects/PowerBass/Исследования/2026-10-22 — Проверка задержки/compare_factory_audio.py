from pathlib import Path
import subprocess,sys,os,select,time,hashlib,json,csv
sys.path.insert(0,'/Users/roman/Library/Caches/PowerKickTools/py')
import numpy as np
r=Path(__file__).parent
old=Path('/Users/roman/Library/Caches/PowerBassBuildUniversal/powerbass_tests_artefacts/Release/powerbass_tests')
new=Path('/Users/roman/Library/Caches/PowerBassBuildDev/powerbass_tests_artefacts/Release/powerbass_tests')
class Server:
 def __init__(self,path):
  self.p=subprocess.Popen([str(path),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=(r/('factory-'+path.parent.parent.parent.name+'.stderr')).open('wb'))
 def render(self,params,flags):
  line=' '.join(str(k)+'='+str(v) for k,v in {**params,**flags,'_len':1.2}.items())+'\n'
  self.p.stdin.write(line.encode());self.p.stdin.flush();need=57600*2*4;chunks=[];deadline=time.monotonic()+30
  while need:
   remain=deadline-time.monotonic()
   if remain<=0 or not select.select([self.p.stdout],[],[],remain)[0]:raise TimeoutError('renderer response timed out')
   data=os.read(self.p.stdout.fileno(),need)
   if not data:raise RuntimeError('renderer terminated early, code '+str(self.p.poll()))
   chunks.append(data);need-=len(data)
  data=b''.join(chunks);x=np.frombuffer(data,dtype='<f4')
  if len(x)!=115200 or not np.isfinite(x).all():raise RuntimeError('invalid audio')
  return data,x
 def close(self):
  self.p.stdin.close()
  try:self.p.wait(timeout=10)
  except subprocess.TimeoutExpired:self.p.terminate();self.p.wait(timeout=10)
  return self.p.returncode
oldhash=hashlib.sha256(old.read_bytes()).hexdigest();newhash=hashlib.sha256(new.read_bytes()).hexdigest()
text=subprocess.run([str(old),'--dump-presets'],capture_output=True,text=True,check=True,timeout=30).stdout
presets=[]
for line in text.splitlines():
 name,category,values=line.split('|');params={k:float(v) for k,v in (t.split('=') for t in values.split())};presets.append((name,category,params))
assert len(presets)==300
servers=[Server(old),Server(new)];maxerr=0;changed=0;activations=[];rows=0
try:
 with (r/'factory-audio-compare.csv').open('w') as f:
  w=csv.writer(f);w.writerow(['preset','category','scenario','max_sample_error','peak_after_dbfs','pre_safety_dbfs_if_active'])
  for n,(name,category,params) in enumerate(presets):
   for label,flags in [('held_C2',{'_pattern':2,'_root':36,'_bpm':150}),('roll16_90',{'_pattern':0,'_root':36,'_bpm':90}),('roll32_210',{'_pattern':0,'_root':36,'_bpm':420})]:
    b,a=servers[0].render(params,flags);c,d=servers[1].render(params,flags)
    error=float(np.max(np.abs(a.astype(np.float64)-d)));maxerr=max(maxerr,error);changed+=b!=c
    pk=float(np.abs(d).max());pre=None
    if pk>.900001:
     _,low=servers[1].render({**params,'master':params['master']-36},flags)
     pl=float(np.abs(low).max());assert pl<.9
     pre=20*np.log10(pl)+36;activations.append({'preset':name,'scenario':label,'pre_safety_dbfs':float(pre),'post_safety_dbfs':float(20*np.log10(pk))})
    w.writerow([name,category,label,error,float(20*np.log10(pk)),pre]);rows+=1;f.flush()
   if n%50==49:print('checked',n+1,'presets',rows,'cases',flush=True)
finally:
 codes=[s.close() for s in servers]
summary={'before_binary_sha256':oldhash,'after_binary_sha256':newhash,'cases':rows,'changed_audio_cases':changed,'max_sample_error':maxerr,'safety_activations':activations,'server_exit_codes':codes,'sample_rate':48000,'root_note':36,'duration':1.2,'limitation':'finite scenario grid; not all notes/velocities/tempos; roll32_210 is same MIDI spacing via renderer bpm420'}
(r/'factory-audio-compare-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
assert hashlib.sha256(old.read_bytes()).hexdigest()==oldhash and hashlib.sha256(new.read_bytes()).hexdigest()==newhash
print('SUMMARY',rows,'cases; changed',changed,'max_error',maxerr,'safety_cases',len(activations),'server_exits',codes)
if changed or any(codes):sys.exit(1)
