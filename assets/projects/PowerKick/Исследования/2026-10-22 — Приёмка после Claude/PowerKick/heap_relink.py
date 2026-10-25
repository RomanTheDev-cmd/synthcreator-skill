from pathlib import Path
import subprocess,json,time,sys,hashlib
out=Path(__file__).resolve().parent; old=sys.argv[1];phase=sys.argv[2];records=[]
source=json.loads((out/(old+"-commands.json")).read_text())
for name in ["compile-WrapperTests","link","tests"]:
 command=next(r for r in source if r["name"]==name);args=command["argv"][:]
 oldobj=str(out/(old+"-WrapperTests.o"));newobj=str(out/(phase+"-WrapperTests.o"))
 args=[newobj if a==oldobj else str(out/phase) if a==str(out/old) else a for a in args]
 t=time.time()
 with (out/(phase+"-"+name+".log")).open("w") as f:r=subprocess.run(args,cwd=command["cwd"],stdout=f,stderr=subprocess.STDOUT)
 records.append(dict(name=name,argv=args,cwd=command["cwd"],exit_code=r.returncode,seconds=time.time()-t));(out/(phase+"-commands.json")).write_text(json.dumps(records,ensure_ascii=False,indent=2));print(name,r.returncode,flush=True)
 if r.returncode:
  print((out/(phase+"-"+name+".log")).read_text()[-2000:],flush=True);sys.exit(r.returncode)
print((out/(phase+"-tests.log")).read_text(),flush=True)
