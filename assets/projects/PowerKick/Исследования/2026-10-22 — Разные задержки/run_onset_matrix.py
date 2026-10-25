from pathlib import Path
import re,shlex,subprocess,json,time
out=Path(__file__).resolve().parent;root=out.parents[1];b=Path.home()/"Library/Caches/PowerKickBuildDev";record=[]
def run(name,args):
 t=time.time()
 with (out/(name+".log")).open("w") as f:r=subprocess.run(args,cwd=b,stdout=f,stderr=subprocess.STDOUT)
 record.append(dict(name=name,argv=args,exit_code=r.returncode,seconds=time.time()-t));(out/"onset-commands.json").write_text(json.dumps(record,ensure_ascii=False,indent=2));print(name,r.returncode,flush=True)
 if r.returncode:raise SystemExit(r.returncode)
flags=(b/"CMakeFiles/powerkick_tests.dir/flags.make").read_text();base=["/usr/bin/c++"]
for key in ["CXX_DEFINES","CXX_INCLUDES","CXX_FLAGS"]:base+=shlex.split(re.search(r"^"+key+r" = (.*)$",flags,re.M)[1])
for phase in ["onset-matrix"]:
 obj=out/(phase+"-benchmark.o");args=base[:]
 if phase=="old-split":args.insert(1,"-I"+str(out/"old-split/Source"))
 run(phase+"-compile",args+["-I"+str(root/"Source/DSP"),"-c",str(out/"onset_matrix.cpp"),"-o",str(obj)])
 link=shlex.split((b/"CMakeFiles/powerkick_tests.dir/link.txt").read_text())
 link=[str(obj) if x=="CMakeFiles/powerkick_tests.dir/Tests/PowerKickTests.cpp.o" else x for x in link if x not in [f"CMakeFiles/powerkick_tests.dir/Source/{n}.cpp.o" for n in ["Params","Presets","KickRenderer"]]]
 link.insert(1,str(root/"Исследования/2026-10-22 — Приёмка после Claude/PowerKick/cc-green-Params.o"));link.insert(1,str(root/"Исследования/2026-10-22 — Приёмка после Claude/PowerKick/cc-green-Presets.o"))
 exe=out/(phase+"-benchmark");link[link.index("-o")+1]=str(exe);run(phase+"-link",link);run(phase+"-run",[str(exe)])

