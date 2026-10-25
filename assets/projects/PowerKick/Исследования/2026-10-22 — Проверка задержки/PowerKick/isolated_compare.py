from pathlib import Path
import subprocess,hashlib,array,json,sys,time
p=Path(__file__).resolve().parent;before=p/'baseline-tests-frozen';after=p/'handoff-green';err=(p/'isolated-compare.stderr.log').open('w');old=subprocess.Popen([str(before),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err);new=subprocess.Popen([str(after),'--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err)
def render(proc,vals):
 line=' '.join(f'{k}={v}' for k,v in vals.items())+'\n';proc.stdin.write(line.encode());proc.stdin.flush();n=int(float(vals['_sr'])*float(vals['_len']))*8;buf=bytearray()
 while len(buf)<n:
  chunk=proc.stdout.read(n-len(buf))
  if not chunk:raise RuntimeError('short output')
  buf+=chunk
 return buf
rows=[];t=time.time()
for line in (p/'preset-dump.log').read_text().splitlines():
 name,cat,vals=line.split('|');v={k:v for k,v in (x.split('=') for x in vals.split())};v.update(_len=.6,_sr=48000);a=render(old,v);b=render(new,v);ha=hashlib.sha256(a).hexdigest();hb=hashlib.sha256(b).hexdigest();maxerr=0.
 if ha!=hb:
  x=array.array('f');x.frombytes(a);y=array.array('f');y.frombytes(b);maxerr=max(abs(s-t) for s,t in zip(x,y))
 rows.append(dict(preset=name,before_sha256=ha,after_sha256=hb,byte_equal=ha==hb,max_abs_error=maxerr))
old.stdin.close();new.stdin.close();codes=[old.wait(),new.wait()];err.close();summary=dict(cases=len(rows),byte_equal=sum(r['byte_equal'] for r in rows),max_abs_error=max(r['max_abs_error'] for r in rows),returncodes=codes,seconds=time.time()-t,before_binary_sha256=hashlib.sha256(before.read_bytes()).hexdigest(),after_binary_sha256=hashlib.sha256(after.read_bytes()).hexdigest());(p/'isolated-compare.json').write_text(json.dumps(dict(summary=summary,rows=rows),indent=2,ensure_ascii=False));print(json.dumps(summary,indent=2));sys.exit(0 if max(codes)==0 and summary['max_abs_error']<1e-5 else 1)
