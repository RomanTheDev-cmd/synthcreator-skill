from pathlib import Path
import shlex,subprocess,json,hashlib,time
root=Path(__file__).parent
project=root.parents[2]
build=Path('/Users/roman/Library/Caches/PowerBassBuildDev')
flags={}
for line in (build/'CMakeFiles/powerbass_tests.dir/flags.make').read_text().splitlines():
 if ' = ' in line:
  k,v=line.split(' = ',1);flags[k]=v
obj=root/'residual.o'
cmd=['/usr/bin/c++']+shlex.split(flags['CXX_DEFINES'])+shlex.split(flags['CXX_INCLUDES'])+shlex.split(flags['CXX_FLAGS'])+['-I'+str(root),'-I'+str(project/'Source/DSP'),'-c',str(root/'residual.cpp'),'-o',str(obj)]
link=shlex.split((build/'CMakeFiles/powerbass_tests.dir/link.txt').read_text())
link=[str(obj) if x.endswith('/Tests/PowerBassTests.cpp.o') else x for x in link]
link[link.index('-o')+1]=str(root/'residual')
results=[]
for name,command in [('compile',cmd),('link',link),('grid',[str(root/'residual')])]:
 start=time.time()
 with (root/('residual-'+name+('.csv' if name=='grid' else '.log'))).open('w') as f, (root/('residual-'+name+'.stderr')).open('w') as err:
  r=subprocess.run(command,cwd=build,stdout=f,stderr=err)
 results.append(dict(stage=name,command=command,returncode=r.returncode,seconds=time.time()-start))
 (root/'residual-execution.json').write_text(json.dumps(results,ensure_ascii=False,indent=2))
 print(name,r.returncode,flush=True)
 if r.returncode:raise SystemExit(r.returncode)
