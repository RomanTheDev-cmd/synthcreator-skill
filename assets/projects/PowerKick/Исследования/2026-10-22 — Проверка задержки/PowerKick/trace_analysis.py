from pathlib import Path
import numpy as np
def lfilter(b,a,x):
    # Independent direct-form I reference; production uses transposed direct-form II.
    out=np.empty(len(x));x1=x2=y1=y2=0.0
    b0=float(b[0]);b1=float(b[1]) if len(b)>1 else 0.;b2=float(b[2]) if len(b)>2 else 0.;a1=float(a[1]) if len(a)>1 else 0.;a2=float(a[2]) if len(a)>2 else 0.
    for i,v in enumerate(x):
        y=b0*v+b1*x1+b2*x2-a1*y1-a2*y2
        out[i]=y;x2=x1;x1=v;y2=y1;y1=y
    return out
import math,json
p=Path(__file__).resolve().parent;sr=48000
presets={}
for line in (p/'preset-dump.log').read_text().splitlines():
 name,cat,vals=line.split('|');presets[name]={k:float(v) for k,v in (x.split('=') for x in vals.split())}
modes=[([(65,7,'low',.7),(280,-5,'peak',.9),(2500,-2,'peak',.7),(7000,-4,'high',.7)],-2.5),([(45,-1,'low',.7),(125,6,'peak',1.2),(420,-4,'peak',1),(3500,4.5,'high',.7)],-2.5),([(60,4,'low',.7),(190,2,'peak',.8),(1500,-5,'peak',.7),(3000,-10,'high',.7)],-1),([(60,2,'low',.7),(300,-3,'peak',1),(1800,6,'peak',.8),(6000,3,'high',.7)],-2.5),([(60,0,'low',.7),(180,-2,'peak',1),(900,7,'peak',1.3),(5000,6,'high',.7)],-3)]
def coefficients(f,gain,kind,q):
 A=10**(gain/40);w=2*math.pi*f/sr;c=math.cos(w);sn=math.sin(w)
 if kind=='peak':
  al=sn/(2*q);b=[1+al*A,-2*c,1-al*A];a=[1+al/A,-2*c,1-al/A]
 else:
  sa=math.sqrt(2*A)*sn
  if kind=='low':b=[A*((A+1)-(A-1)*c+sa),2*A*((A-1)-(A+1)*c),A*((A+1)-(A-1)*c-sa)];a=[(A+1)+(A-1)*c+sa,-2*((A-1)+(A+1)*c),(A+1)+(A-1)*c-sa]
  else:b=[A*((A+1)+(A-1)*c+sa),-2*A*((A-1)+(A+1)*c),A*((A+1)+(A-1)*c-sa)];a=[(A+1)-(A-1)*c+sa,2*((A-1)-(A+1)*c),(A+1)-(A-1)*c-sa]
 return np.array(b)/a[0],np.array(a)/a[0]
def processEq(x,params):
 bands,makeup=modes[int(params['charMode'])]
 for f,g,k,q in bands:
  b,a=coefficients(f,g*params['character'],k,q);x=lfilter(b,a,x)
 return x*10**(makeup*params['character']/20)
def post(x,params):return lfilter([1,-1],[1,-math.exp(-2*math.pi*12/sr)],x)*10**((params['output']-24)/20)
rows=[]
for name in ['Warm Click F#1 III','Classic Punch','Crushed Click A#1']:
 pr=presets[name]
 for case in ['single','roll','soft']:
  a=np.fromfile(p/(name+'-'+case+'.f64'),dtype='<f8').reshape(-1,12);eq=processEq(a[:,4],pr);dc=post(eq,pr);peak=int(np.argmax(np.max(np.abs(a[:,7:9]),axis=1)));midpeak=int(np.argmax(abs(a[:,6])));new=np.where(a[:,3]==0)[0]
  # Split at the actual new voice onset, without resetting any filter states.
  onset=int(new[new<=peak][-1]);old=a[:,4].copy();old[onset:]=0;oldEq=processEq(old,pr);oldDC=post(oldEq,pr);newDC=dc-oldDC
  r=dict(preset=name,case=case,peak_sample=peak,peak_hit=peak//1714 if case=='roll' else 0,actual_new_onset=onset,eq_model_max_abs_error=float(np.max(abs(eq-a[:,5]))),dc_model_max_abs_error=float(np.max(abs(dc-a[:,6]))),at_peak=dict(body=float(a[peak,0]),click=float(a[peak,1]),rumble=float(a[peak,2]),pre_eq=float(a[peak,4]),post_eq=float(a[peak,5]),mid=float(a[peak,6]),left=float(a[peak,7]),right=float(a[peak,8]),old_history_mid=float(oldDC[peak]),new_since_onset_mid=float(newDC[peak])),voice_overlap_samples=int(np.count_nonzero((a[:,3]>0)&(a[:,3]<.0001))))
  rows.append(r)
(p/'trace-analysis.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2));print(json.dumps(rows,ensure_ascii=False,indent=2))
