from pathlib import Path
import re,shlex,subprocess,json,sys,time
p=Path(__file__).resolve().parent;root=p.parents[2];b=Path.home()/'Library/Caches/PowerKickBuildDev';phase=sys.argv[1];meta=[]
flags=(b/'CMakeFiles/powerkick_tests.dir/flags.make').read_text();base=['/usr/bin/c++']
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:base+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
def run(name,args,cwd=b):
 t=time.time()
 with (p/(phase+'-'+name+'.log')).open('w') as f:r=subprocess.run(args,cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
 meta.append(dict(name=name,argv=args,cwd=str(cwd),returncode=r.returncode,seconds=time.time()-t));(p/(phase+'-commands.json')).write_text(json.dumps(meta,indent=2,ensure_ascii=False));print(name,r.returncode,flush=True)
 if r.returncode:print((p/(phase+'-'+name+'.log')).read_text()[-4000:],flush=True);sys.exit(r.returncode)
for src,obj in [('Tests/PowerKickTests.cpp','PowerKickTests.new.o'),('Source/KickRenderer.cpp','KickRenderer.new.o')]:
 source = p/('PowerKickTests.prototype.cpp' if src.startswith('Tests') else 'KickRenderer.prototype.cpp') if phase=='prototype3-tests' else root/src
 run('compile-'+obj,base+['-c',str(source),'-o',str(p/(phase+obj))])
link=shlex.split((b/'CMakeFiles/powerkick_tests.dir/link.txt').read_text());link=[str(p/(phase+'PowerKickTests.new.o')) if x=='CMakeFiles/powerkick_tests.dir/Tests/PowerKickTests.cpp.o' else str(p/(phase+'KickRenderer.new.o')) if x=='CMakeFiles/powerkick_tests.dir/Source/KickRenderer.cpp.o' else x for x in link];link[link.index('-o')+1]=str(p/phase);run('link',link);run('tests',[str(p/phase)])
print((p/(phase+'-tests.log')).read_text()[-2000:],flush=True)
