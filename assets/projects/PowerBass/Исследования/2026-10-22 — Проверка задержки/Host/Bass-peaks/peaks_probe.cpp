#include "BassEngineCurrentProbe.h"
#include "Presets.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <filesystem>
using namespace pb;
struct Note {double on,off;int note;};
int main(int argc,char**argv) {
 if(argc!=3)return 2;
 std::filesystem::path out(argv[2]);std::filesystem::create_directories(out);
 std::ifstream input(argv[1]);std::string line;
 std::cout<<std::setprecision(13);
 while(std::getline(input,line)) {
  auto a=line.find('|'),b=line.find('|',a+1);
  const std::string name=line.substr(0,a);
  Snapshot snapshot;std::istringstream values(line.substr(b+1));std::string t;
  while(values>>t) {auto x=t.find('=');snapshot[juce::String(t.substr(0,x))]=std::stof(t.substr(x+1));}
  const int bpm=name=="Heavy Reese 02"||name=="Reese Core"?420:90;
  constexpr int sr=48000,total=57600,block=512;
  const double step=60.0/bpm/4.0;
  std::vector<Note> notes;
  for(int k=0;k*step<1.2-step;k++) {
   if(k%4==0)continue;
   int note=36+((k/16)%2==1 && k%16>=12?3:0);
   notes.push_back({k*step,k*step+0.75*step,note});
  }
  for(int variant:{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17}) {
   auto p=paramsFromSnapshot(snapshot);
   if(variant==4)p.punch=0;
   if(variant==5)p.width=0;
   if(variant==6)p.distMix=0;
   if(variant==7)p.envAmount=0;
   dsp::BassEngine e;e.probeVariant=variant;e.prepare(sr,block);e.probeTrace.reserve(total+block);
   juce::AudioBuffer<float> audio(2,total+block);audio.clear();
   for(int pos=0;pos<total;pos+=block) {
    juce::MidiBuffer midi;
    for(auto n:notes) {
     int on=(int)(n.on*sr),off=(int)(n.off*sr);
     if(on>=pos && on<pos+block)midi.addEvent(juce::MidiMessage::noteOn(1,n.note,1.0f),on-pos);
     if(off>=pos && off<pos+block)midi.addEvent(juce::MidiMessage::noteOff(1,n.note),off-pos);
    }
    e.process(audio.getWritePointer(0,pos),audio.getWritePointer(1,pos),block,midi,p);
   }
   double peak=0,post=0,maxVoice=0,maxDist=0,maxFilter=0,maxBase=0,maxEq=0;int at=0,above=0;
   for(int i=0;i<total;i++) {
    auto f=e.probeTrace[(size_t)i];auto pk=std::max(std::abs(f.preL),std::abs(f.preR));
    if(pk>peak){peak=pk;at=i;}above+=pk>.9;
    post=std::max(post,(double)std::max(std::abs(audio.getSample(0,i)),std::abs(audio.getSample(1,i))));
    maxVoice=std::max(maxVoice,f.voiceMax);maxDist=std::max(maxDist,f.distMax);
    maxFilter=std::max(maxFilter,f.postFilterMax);maxBase=std::max(maxBase,std::abs(f.base));maxEq=std::max(maxEq,std::abs(f.eq));
   }
   auto f=e.probeTrace[(size_t)at];
   std::cout<<"{\"preset\":\""<<name<<"\",\"bpm_server\":"<<bpm<<",\"variant\":"<<variant
    <<",\"pre_peak_dbfs\":"<<20*std::log10(peak)<<",\"post_peak_dbfs\":"<<20*std::log10(post)<<",\"peak_sample\":"<<at
    <<",\"samples_over_safety\":"<<above<<",\"max_dist\":"<<maxDist<<",\"max_filter\":"<<maxFilter<<",\"max_voice\":"<<maxVoice<<",\"max_base\":"<<maxBase<<",\"max_eq\":"<<maxEq
    <<",\"cut_at_peak\":"<<f.cut<<",\"amp_at_peak\":"<<f.amp<<",\"flt_env_at_peak\":"<<f.fltEnv<<"}\n";
   if(variant==0) {
    auto prefix=name;std::replace(prefix.begin(),prefix.end(),' ','_');
    std::ofstream trace(out/(prefix+"-trace.f64"),std::ios::binary);
    trace.write(reinterpret_cast<const char*>(e.probeTrace.data()),total*sizeof(dsp::BassEngine::ProbeFrame));
    std::vector<float> interleaved(total*2);
    for(int i=0;i<total;i++)for(int c=0;c<2;c++)interleaved[i*2+c]=audio.getSample(c,i);
    std::ofstream wave(out/(prefix+"-baseline.f32"),std::ios::binary);
    wave.write(reinterpret_cast<const char*>(interleaved.data()),interleaved.size()*sizeof(float));
   }
  }
 }
 return 0;
}
