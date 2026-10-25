from pathlib import Path
import re,shlex,subprocess,json,sys,time
p=Path(__file__).resolve().parent;root=p.parents[2];b=Path.home()/'Library/Caches/PowerKickBuildDev';phase=sys.argv[1];meta=[]
def command(name,args):
 t=time.time()
 with (p/(phase+'-'+name+'.log')).open('w') as f:r=subprocess.run(args,cwd=b,stdout=f,stderr=subprocess.STDOUT)
 meta.append(dict(name=name,argv=args,cwd=str(b),returncode=r.returncode,seconds=time.time()-t));print(name,r.returncode,flush=True)
 (p/(phase+'-commands.json')).write_text(json.dumps(meta,ensure_ascii=False,indent=2))
 if r.returncode:print((p/(phase+'-'+name+'.log')).read_text()[-3000:]);sys.exit(r.returncode)
def compile(source,obj,target):
 flags=(b/f'CMakeFiles/{target}.dir/flags.make').read_text();args=['/usr/bin/c++']
 for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:args+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
 args+=['-c',str(source),'-o',str(obj)];command('compile-'+obj.stem,args)
if phase=='measure':
 compile(p/'independent.cpp',p/'independent.o','powerkick_tests')
 link=shlex.split((b/'CMakeFiles/powerkick_tests.dir/link.txt').read_text());link=[str(p/'independent.o') if x=='CMakeFiles/powerkick_tests.dir/Tests/PowerKickTests.cpp.o' else x for x in link];link[link.index('-o')+1]=str(p/'independent');command('link',link);command('timing',[str(p/'independent')])
else:
 compile(root/'Tests/WrapperTests.cpp',p/'WrapperTests.o','powerkick_wrapper_tests')
 link=shlex.split((b/'CMakeFiles/powerkick_wrapper_tests.dir/link.txt').read_text());link=[str(p/'WrapperTests.o') if x=='CMakeFiles/powerkick_wrapper_tests.dir/Tests/WrapperTests.cpp.o' else x for x in link]
 if phase in ('green', 'prepare-red'):
  for cpp in ['Params','Presets','KickRenderer','PluginProcessor','PluginEditor']:
   obj=p/(cpp+'.wrapper.o');compile(root/('Source/'+cpp+'.cpp'),obj,'PowerKick');link.insert(link.index('PowerKick_artefacts/Release/libPowerKick_SharedCode.a'),str(obj))
 link[link.index('-o')+1]=str(p/('wrapper-'+phase));command('link',link)
 args=[str(p/('wrapper-'+phase))];t=time.time()
 with (p/(phase+'-test.log')).open('w') as f:r=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT)
 meta.append(dict(name='test',argv=args,returncode=r.returncode,seconds=time.time()-t));(p/(phase+'-commands.json')).write_text(json.dumps(meta,ensure_ascii=False,indent=2));print((p/(phase+'-test.log')).read_text(),flush=True);sys.exit(r.returncode)
