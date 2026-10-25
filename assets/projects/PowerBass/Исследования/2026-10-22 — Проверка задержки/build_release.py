from pathlib import Path
import subprocess,sys,json,time
r=Path(__file__).parent
root=r.parents[1]
cmake='/Users/roman/Library/Caches/PowerKickTools/cmake/cmake/data/bin/cmake'
platform=sys.argv[1]
build=Path('/Users/roman/Library/Caches/PowerBassBuild'+('Universal' if platform=='mac' else 'Win'))
commands=[]
steps=[]
if platform=='mac':steps.append(('configure',[cmake,'-S',str(root),'-B',str(build),'-DPOWERBASS_COPY_AFTER_BUILD=OFF']))
steps.append(('build',[cmake,'--build',str(build),'-j2' if platform=='mac' else '-j1']))
if platform=='mac':
 steps += [('dsp',[str(build/'powerbass_tests_artefacts/Release/powerbass_tests')]),('wrapper',[str(build/'powerbass_wrapper_tests_artefacts/Release/powerbass_wrapper_tests')])]
for label,cmd in steps:
 t=time.monotonic()
 with (r/('release-'+platform+'-'+label+'.log')).open('w') as f: rc=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT).returncode
 commands.append({'name':label,'argv':cmd,'returncode':rc,'seconds':time.monotonic()-t})
 (r/('release-'+platform+'-commands.json')).write_text(json.dumps(commands,ensure_ascii=False,indent=2)+'\n')
 print(label,rc,flush=True)
 if rc:sys.exit(rc)
