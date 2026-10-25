#include "BassEngineTransition.h"
#include "Presets.h"
#include <atomic>
#include <thread>
#include <iostream>
#include <mutex>
#include <iomanip>
using namespace pb;
int main(){ const auto& ps=factoryPresets(); std::atomic<int> next{0}; std::mutex mutex;
 const std::vector<std::string> names={"Biting Line 03","Heavy Rumble 01","Dark FM 07","Hard Reese 06","Heavy Reese 02","Heavy FM 04","Dark FM 05","Cellar Line","Evil Reese","Dark Reese 12","Reese Core","Deep Sub"};
 std::cout<<"preset,bpm,division,offkick,mode,pre_peak_dbfs,safety_samples,peak_sample\n";
 auto work=[&]{for(int ix;(ix=next++)<(int)ps.size();){if(std::find(names.begin(),names.end(),ps[ix].name.toStdString())==names.end())continue;
 auto p=paramsFromSnapshot(ps[ix].values);
 for(int bpm:{90,150,210}) for(int div:{16,32}) for(int off:{0,1}) for(int mode:{0,7,8,9}){
 constexpr int sr=48000,block=512,total=144000;dsp::BassEngine e;e.auditMode=mode;e.prepare(sr,block,p);
 dsp::BassEngine::auditPeak=0;dsp::BassEngine::auditSafetySamples=0;dsp::BassEngine::auditSample=0;
 const double step=60./bpm*4./div;juce::MidiBuffer all;
 for(int k=0;k*step<3.-step;k++){if(off&&k%4==0)continue;all.addEvent(juce::MidiMessage::noteOn(1,36,1.f),(int)(k*step*sr));all.addEvent(juce::MidiMessage::noteOff(1,36),(int)((k+.75)*step*sr));}
 float l[block],r[block];for(int pos=0;pos<total;pos+=block)e.process(l,r,std::min(block,total-pos),all,p,pos);
 std::lock_guard<std::mutex> lock(mutex);std::cout<<ps[ix].name<<','<<bpm<<','<<div<<','<<off<<','<<mode<<','<<20*std::log10(std::max(1.e-15,dsp::BassEngine::auditPeak))<<','<<dsp::BassEngine::auditSafetySamples<<','<<dsp::BassEngine::auditPeakSample<<'\n';
 }}};std::vector<std::thread> ts;for(int i=0;i<4;i++)ts.emplace_back(work);for(auto& t:ts)t.join();}
