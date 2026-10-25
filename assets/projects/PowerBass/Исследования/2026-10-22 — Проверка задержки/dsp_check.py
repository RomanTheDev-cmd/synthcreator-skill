from pathlib import Path
import subprocess,sys
r=Path(__file__).parent; phase=sys.argv[1]
b=Path('/Users/roman/Library/Caches/PowerBassBuildDev')
cmake='/Users/roman/Library/Caches/PowerKickTools/cmake/cmake/data/bin/cmake'
for name,cmd in [('build',[cmake,'--build',str(b),'--target','powerbass_tests','-j2']),('test',[str(b/'powerbass_tests_artefacts/Release/powerbass_tests')])]:
 with (r/(phase+'-'+name+'.log')).open('w') as f: rc=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT).returncode
 (r/(phase+'-'+name+'.exit')).write_text(str(rc));print(name,rc,flush=True)
 if rc:sys.exit(rc)
