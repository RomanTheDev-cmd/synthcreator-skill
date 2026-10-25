#define main render_probe_main
#include "au_probe.cpp"
#undef main
#include <cctype>

void setNamed(AU& a, const std::string& want, float value) {
 UInt32 bytes=0;Boolean writable=false;
 check(AudioUnitGetPropertyInfo(a.u,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,&bytes,&writable),"list size");
 std::vector<AudioUnitParameterID> ids(bytes/sizeof(AudioUnitParameterID));
 check(AudioUnitGetProperty(a.u,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,ids.data(),&bytes),"list");
 for(auto id:ids) {
  AudioUnitParameterInfo inf {};bytes=sizeof(inf);
  check(AudioUnitGetProperty(a.u,kAudioUnitProperty_ParameterInfo,kAudioUnitScope_Global,id,&inf,&bytes),"info");
  char s[1024] {};
  if(inf.cfNameString)CFStringGetCString(inf.cfNameString,s,sizeof(s),kCFStringEncodingUTF8);
  else std::copy(inf.name,inf.name+std::min(sizeof(inf.name),sizeof(s)-1),s);
  std::string name(s);for(char&c:name)c=std::tolower((unsigned char)c);
  if(inf.cfNameString && (inf.flags&kAudioUnitParameterFlag_CFNameRelease))CFRelease(inf.cfNameString);
  if(name==want) {check(AudioUnitSetParameter(a.u,id,kAudioUnitScope_Global,0,value,0),"set named");return;}
 }
 throw std::runtime_error("named parameter not found: "+want);
}
void reprepare(AU& a,double sr) {
 check(AudioUnitUninitialize(a.u),"uninitialize");
 AudioStreamBasicDescription f {};UInt32 n=sizeof(f);
 check(AudioUnitGetProperty(a.u,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&f,&n),"get format");
 f.mSampleRate=sr;
 check(AudioUnitSetProperty(a.u,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&f,sizeof(f)),"set new rate");
 check(AudioUnitInitialize(a.u),"reinitialize");
}
Run sample(AU& a,int sr) {
 Run r;r.l.resize(sr*2);r.r.resize(sr*2);
 struct { UInt32 count;AudioBuffer b[2]; } abl;
 for(int pos=0;pos<sr*2;pos+=512) {
  int count=std::min(512,sr*2-pos);
  for(auto event: {std::pair<int,int>{sr,0x90},{sr+sr/2,0x80}})
   if(event.first>=pos && event.first<pos+count)
    check(MusicDeviceMIDIEvent(a.u,event.second,36,event.second==0x90?100:0,event.first-pos),"MIDI");
  abl.count=2;abl.b[0]={1,(UInt32)count*4,r.l.data()+pos};abl.b[1]={1,(UInt32)count*4,r.r.data()+pos};
  AudioTimeStamp ts {};ts.mFlags=kAudioTimeStampSampleTimeValid;ts.mSampleTime=pos;
  AudioUnitRenderActionFlags flags=0;
  check(AudioUnitRender(a.u,&flags,&ts,0,count,reinterpret_cast<AudioBufferList*>(&abl)),"render");
 }
 return r;
}
int main() {
 try {
  for(float width: {0.0f,1.0f}) {
   AU reused('Pwbs',48000);setNamed(reused,"width",width);
   sample(reused,48000);reprepare(reused,96000);
   const auto actual=sample(reused,96000);
   AU fresh('Pwbs',96000);setNamed(fresh,"width",width);
   const auto reference=sample(fresh,96000);
   auto d=difference(reference,actual);
   double mid=0,side=0;
   for(size_t i=0;i<actual.l.size();i++) {
    mid=std::max(mid,std::abs((actual.l[i]+actual.r[i]-reference.l[i]-reference.r[i])*0.5));
    side=std::max(side,std::abs((actual.l[i]-actual.r[i]-reference.l[i]+reference.r[i])*0.5));
   }
   std::cout<<std::setprecision(12)<<"{\"width\":"<<width<<",\"max_channel_diff\":"<<d<<",\"max_mid_diff\":"<<mid<<",\"max_side_diff\":"<<side<<"}\n";
  }
 } catch(const std::exception&e) {std::cerr<<e.what()<<"\n";return 2;}
 return 0;
}
