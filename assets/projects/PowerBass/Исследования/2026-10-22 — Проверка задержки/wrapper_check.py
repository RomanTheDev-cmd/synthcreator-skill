from pathlib import Path
import subprocess,sys
r=Path(__file__).parent
cmake='/Users/roman/Library/Caches/PowerKickTools/cmake/cmake/data/bin/cmake'
build=Path('/Users/roman/Library/Caches/PowerBassBuildDev')
phase=sys.argv[1]
for label,cmd in [('build',[cmake,'--build',str(build),'--target','powerbass_wrapper_tests','-j2']),('test',[str(build/'powerbass_wrapper_tests_artefacts/Release/powerbass_wrapper_tests')])]:
 with (r/(phase+'-'+label+'.log')).open('w') as f: rc=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT).returncode
 (r/(phase+'-'+label+'.exit')).write_text(str(rc));print(label,rc,flush=True)
 if rc: sys.exit(rc)
