from pathlib import Path
import subprocess,json,array,math,sys
p=Path(__file__).resolve().parent;b=Path.home()/'Library/Caches/PowerKickBuildDev/powerkick_tests_artefacts/Release/powerkick_tests'
r=subprocess.run([str(b),'--dump-presets'],capture_output=True,text=True);(p/'preset-dump.log').write_text(r.stdout);presets={}
for line in r.stdout.splitlines():
 name,cat,vals=line.split('|');presets[name]={k:float(v) for k,v in (x.split('=') for x in vals.split())}
err=(p/'soft-single-control.stderr.log').open('w');proc=subprocess.Popen([str(b),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err);rows=[]
for name,bpm,vel,step in [('Clipped Rumble A1 II',150,.8,0),('Clipped Clang F#1 II',150,.8,0),('Warm Clang E1',150,.8,0)]:
 for delta in [-24,0]:
  vals={**presets[name]};vals['output']+=delta;vals.update(_len=1,_sr=48000,_bpm=bpm,_vel=vel,_step=step,_alt=1 if step else 0)
  line=' '.join(f'{k}={v:.9g}' for k,v in vals.items())+'\n';proc.stdin.write(line.encode());proc.stdin.flush();n=48000*2*4;buf=bytearray()
  while len(buf)<n:
   chunk=proc.stdout.read(n-len(buf))
   if not chunk:raise RuntimeError('short read')
   buf+=chunk
  a=array.array('f');a.frombytes(buf);peak=max(abs(x) for x in a);rows.append(dict(preset=name,bpm=bpm,velocity=vel,step=step,output_delta=delta,measured_dbfs=20*math.log10(peak),extrapolated_dbfs=20*math.log10(peak)-delta,finite=all(math.isfinite(x) for x in a)))
proc.stdin.close();code=proc.wait();err.close();(p/'soft-single-control.json').write_text(json.dumps(dict(argv=[str(b),'--serve'],returncode=code,rows=rows),ensure_ascii=False,indent=2));print(json.dumps(rows,ensure_ascii=False,indent=2));sys.exit(code)
