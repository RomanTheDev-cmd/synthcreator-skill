#include "BassEngineResidual.h"
#include "Presets.h"
#include <iostream>
#include <fstream>
#include <filesystem>
using namespace pb;
int main(int argc,char**argv){if(argc!=2)return 2;std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
 const auto& ps=factoryPresets();std::vector<std::string> names={"Reese Core","Heavy Rumble 01","Biting Line 03","Dark FM 07","Dark Reese 12","Heavy Reese 02"};
 std::cout<<"preset,scenario,mode,pre_peak_dbfs,safety_samples,rms_dbfs,file\n";
 for(const auto& preset:ps){if(std::find(names.begin(),names.end(),preset.name.toStdString())==names.end())continue;
 for(int scenario:{0,1,2,3})for(int mode:{0,2}){
 auto p=paramsFromSnapshot(preset.values);if(scenario==3)p.distMix=0;
 constexpr int sr=48000,block=512,total=144000;dsp::BassEngine e;e.auditMode=mode;e.auditCapture=true;e.auditRaw.reserve(total*2);e.prepare(sr,block,p);dsp::BassEngine::auditPeak=0;dsp::BassEngine::auditSafetySamples=0;
 juce::MidiBuffer all;
 if(scenario==0||scenario==3){all.addEvent(juce::MidiMessage::noteOn(1,36,1.f),0);all.addEvent(juce::MidiMessage::noteOff(1,36),72000);}
 else{double step=scenario==1?60./90/4:60./210/8;for(int k=0;k*step<3-step;k++){if(scenario==1&&k%4==0)continue;all.addEvent(juce::MidiMessage::noteOn(1,36,1.f),(int)(k*step*sr));all.addEvent(juce::MidiMessage::noteOff(1,36),(int)((k+.75)*step*sr));}}
 float l[block],r[block];for(int pos=0;pos<total;pos+=block)e.process(l,r,std::min(block,total-pos),all,p,pos);
 double sum=0;for(float x:e.auditRaw)sum+=x*x;
 auto name=preset.name.toStdString();std::replace(name.begin(),name.end(),' ','_');name+="-s"+std::to_string(scenario)+"-m"+std::to_string(mode)+".f32";std::ofstream f(out/name,std::ios::binary);f.write((char*)e.auditRaw.data(),(long)(e.auditRaw.size()*sizeof(float)));
 std::cout<<preset.name<<','<<scenario<<','<<mode<<','<<20*std::log10(dsp::BassEngine::auditPeak)<<','<<dsp::BassEngine::auditSafetySamples<<','<<10*std::log10(sum/e.auditRaw.size())<<','<<name<<'\n';
 }} }
