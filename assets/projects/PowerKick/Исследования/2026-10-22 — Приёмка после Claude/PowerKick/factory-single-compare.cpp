#include "DSP/KickEngine.h"
#include "Presets.h"
#include "BaselineSingleEngine.h"
#include <CommonCrypto/CommonDigest.h>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <sstream>
std::string hash(const std::vector<float>& x) {
 unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(x.data(),(CC_LONG)(x.size()*sizeof(float)),digest);
 std::ostringstream s;for(auto d:digest)s<<std::hex<<std::setw(2)<<std::setfill('0')<<(int)d;return s.str();
}
int main() {
 int index=0,bad=0;constexpr int frames=192000;
 std::cout<<"index\tpreset\tos\toutput_db\tframes\tmax_abs_diff\tbefore_sha256\tafter_sha256\n";
 for(const auto& pr:pk::factoryPresets()) {
  auto p=pk::paramsFromSnapshot(pr.values);
  pk::baseline::KickEngine a;pk::dsp::KickEngine b;a.prepare(48000,512,p.oversampleIndex);b.prepare(48000,512,p.oversampleIndex);
  std::vector<float> before(frames*2),after(frames*2);
  // Natural factory decay plus an explicit MIDI note-off at 250ms, and the full following tail.
  for(int pos=0;pos<frames;pos+=512) {
   int n=std::min(512,frames-pos);juce::MidiBuffer m;
   if(pos==0)m.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);
   if(pos<=12000 && pos+n>12000)m.addEvent(juce::MidiMessage::noteOff(1,60),12000-pos);
   a.process(before.data()+pos,before.data()+frames+pos,n,m,0,p,140,false);
   b.process(after.data()+pos,after.data()+frames+pos,n,m,0,p,140,false);
  }
  double diff=0;for(size_t i=0;i<before.size();++i)diff=std::max(diff,std::abs((double)before[i]-after[i]));if(diff!=0)++bad;
  std::cout<<++index<<"\t"<<pr.name<<"\t"<<p.oversampleIndex<<"\t"<<p.outputDb<<"\t"<<frames<<"\t"<<std::setprecision(17)<<diff<<"\t"<<hash(before)<<"\t"<<hash(after)<<"\n";
 }
 std::cerr<<"factory singles: "<<index<<", changed: "<<bad<<"\n";return bad?1:0;
}
