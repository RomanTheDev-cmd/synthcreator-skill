#define main render_probe_main
#include "au_probe.cpp"
#undef main
#include <cstring>
int main() {
 try {
  std::cout << std::setprecision(18);
  for(auto pr: {std::pair<const char*,OSType>{"PowerKick",'Pwkk'},{"PowerBass",'Pwbs'}}) {
   AU a(pr.second,48000);
   UInt32 size=0;Boolean writable=false;
   check(AudioUnitGetPropertyInfo(a.u,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,&size,&writable),"parameter list size");
   std::vector<AudioUnitParameterID> ids(size/sizeof(AudioUnitParameterID));
   check(AudioUnitGetProperty(a.u,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,ids.data(),&size),"parameter list");
   for(auto id:ids) {
    AudioUnitParameterInfo info {};size=sizeof(info);
    check(AudioUnitGetProperty(a.u,kAudioUnitProperty_ParameterInfo,kAudioUnitScope_Global,id,&info,&size),"parameter info");
    char name[1024] {};
    if(info.cfNameString) CFStringGetCString(info.cfNameString,name,sizeof(name),kCFStringEncodingUTF8);
    else std::strncpy(name,info.name,sizeof(name)-1);
    AudioUnitParameterValue old=0,got=0;
    check(AudioUnitGetParameter(a.u,id,kAudioUnitScope_Global,0,&old),"get old");
    check(AudioUnitSetParameter(a.u,id,kAudioUnitScope_Global,0,info.defaultValue,0),"set default");
    check(AudioUnitGetParameter(a.u,id,kAudioUnitScope_Global,0,&got),"get default roundtrip");
    if(got!=info.defaultValue || std::string(name).find("Oversampl")!=std::string::npos)
     std::cout << "{\"plugin\":\""<<pr.first<<"\",\"id\":"<<id<<",\"name\":\""<<name<<"\",\"info_default\":"<<info.defaultValue<<",\"got\":"<<got<<",\"delta\":"<<(double)got-info.defaultValue<<",\"original_value\":"<<old<<",\"min\":"<<info.minValue<<",\"max\":"<<info.maxValue<<"}\n";
    check(AudioUnitSetParameter(a.u,id,kAudioUnitScope_Global,0,old,0),"restore instance parameter");
    if(info.cfNameString && (info.flags & kAudioUnitParameterFlag_CFNameRelease))CFRelease(info.cfNameString);
   }
  }
 } catch(const std::exception&e) { std::cerr<<e.what()<<"\n";return 2; }
 return 0;
}
