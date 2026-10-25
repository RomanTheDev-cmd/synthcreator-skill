from pathlib import Path
import re,shlex,subprocess,json,sys,time,hashlib
out=Path(__file__).resolve().parent; root=out.parents[2]; build=Path.home()/"Library/Caches/PowerKickBuildDev"
phase=sys.argv[1]; mode=sys.argv[2]; records=[]
def run(name,args,cwd=build):
 t=time.time()
 with (out/(phase+"-"+name+".log")).open("w") as f: r=subprocess.run(args,cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
 records.append(dict(name=name,argv=args,cwd=str(cwd),exit_code=r.returncode,seconds=time.time()-t))
 (out/(phase+"-commands.json")).write_text(json.dumps(records,ensure_ascii=False,indent=2));print(name,r.returncode,flush=True)
 if r.returncode:
  print((out/(phase+"-"+name+".log")).read_text()[-3500:],flush=True);sys.exit(r.returncode)
def compile(src,name,target):
 flags=(build/f"CMakeFiles/{target}.dir/flags.make").read_text();args=["/usr/bin/c++"]
 for key in ["CXX_DEFINES","CXX_INCLUDES","CXX_FLAGS"]: args+=shlex.split(re.search(r"^"+key+r" = (.*)$",flags,re.M)[1])
 obj=out/(phase+"-"+name+".o");run("compile-"+name,args+["-c",str(src),"-o",str(obj)]);return str(obj)
target="powerkick_wrapper_tests" if mode.startswith("wrapper") else "powerkick_tests"
link=shlex.split((build/f"CMakeFiles/{target}.dir/link.txt").read_text())
if mode.startswith("wrapper"):
 obj=compile(root/"Tests/WrapperTests.cpp" if mode=="wrapper" else out/("heap-focused.cpp" if "heap" in mode else "mono-focused.cpp"),"WrapperTests",target)
 link=[obj if x=="CMakeFiles/powerkick_wrapper_tests.dir/Tests/WrapperTests.cpp.o" else x for x in link]
 for n in ["Params","Presets","KickRenderer","PluginProcessor","PluginEditor"]:
  obj=compile(out/"old-mono-PluginProcessor.cpp" if mode=="wrapper-heap-red" and n=="PluginProcessor" else root/f"Source/{n}.cpp",n,"PowerKick");link.insert(link.index("PowerKick_artefacts/Release/libPowerKick_SharedCode.a"),obj)
else:
 for n in ["Params","Presets","KickRenderer"]:
  obj=compile(root/f"Source/{n}.cpp",n,target)
  link=[obj if x==f"CMakeFiles/powerkick_tests.dir/Source/{n}.cpp.o" else x for x in link]
 src=root/"Tests/PowerKickTests.cpp" if mode=="full" else out/(mode+".cpp")
 obj=compile(src,"Tests",target);link=[obj if x=="CMakeFiles/powerkick_tests.dir/Tests/PowerKickTests.cpp.o" else x for x in link]
exe=out/phase;link[link.index("-o")+1]=str(exe);run("link",link);run("tests",[str(exe)],root)
print((out/(phase+"-tests.log")).read_text()[-2500:],flush=True)
