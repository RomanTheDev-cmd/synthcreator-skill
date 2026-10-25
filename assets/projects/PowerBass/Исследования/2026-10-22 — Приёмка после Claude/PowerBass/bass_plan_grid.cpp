#include "BassEngineAudit.h"
#include "Presets.h"
#include <atomic>
#include <thread>
#include <iostream>
#include <mutex>
#include <iomanip>
using namespace pb;
int main() {
 const auto& presets=factoryPresets(); std::atomic<int> next{0}; std::mutex output;
 std::cout<<"preset,bpm,division,velocity,gate_pattern,pre_peak_dbfs,safety_samples\n";
 auto work=[&] {
 for(int ix;(ix=next++)<(int)presets.size();) {
  auto p=paramsFromSnapshot(presets[(size_t)ix].values);
  for(int bpm:{90,150,210}) for(int division:{16,32}) for(float vel:{1.f,.7f}) for(int offkick:{0,1}) {
   constexpr int sr=48000,block=512,total=sr*3;
   dsp::BassEngine e;e.prepare(sr,block,p);dsp::BassEngine::auditPeak=0;dsp::BassEngine::auditSafetySamples=0;
   const double step=60./bpm*4./division;juce::MidiBuffer all;
   for(int k=0;k*step<3.-step;k++) {if(offkick && k%4==0)continue;
    all.addEvent(juce::MidiMessage::noteOn(1,36,vel),(int)(k*step*sr));
    all.addEvent(juce::MidiMessage::noteOff(1,36),(int)((k+.75)*step*sr));}
   float l[block],r[block];for(int pos=0;pos<total;pos+=block)e.process(l,r,std::min(block,total-pos),all,p,pos);
   std::lock_guard<std::mutex> guard(output);
   std::cout<<presets[(size_t)ix].name<<','<<bpm<<','<<division<<','<<vel<<','<<offkick<<','<<20*std::log10(std::max(1.e-15,dsp::BassEngine::auditPeak))<<','<<dsp::BassEngine::auditSafetySamples<<'\n';
  }
 }
 };std::vector<std::thread> threads;for(int i=0;i<6;i++)threads.emplace_back(work);for(auto& t:threads)t.join();
}
