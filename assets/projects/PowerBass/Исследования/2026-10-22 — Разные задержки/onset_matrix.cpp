#include "DSP/BassEngine.h"
#include "Presets.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <iostream>
using namespace pb;
int main(){constexpr int sr=48000,block=512,noteFrames=9600;const auto& presets=factoryPresets();std::atomic<int>next{0};std::mutex lock;
 std::cout<<"preset,scenario,os,attack_ms,stereo_delay_ms,width,glide_ms,reported_latency,onset60_ms,onset40_ms,peak30_ms,peak30,max_diff_vs_cold\n";
 auto work=[&]{for(int ix;(ix=next++)<(int)presets.size();){auto original=paramsFromSnapshot(presets[(size_t)ix].values);std::vector<float> cold[2];
 for(int scene:{0,1,2,3}){auto p=original;bool warm=scene%2;int group=scene/2;if(group)p.stereoDelayMs=0;int at=warm?sr:0,total=at+noteFrames;std::vector<float>l(total),r(total);
 dsp::BassEngine e;e.prepare(sr,block,p);
 for(int pos=0;pos<total;pos+=block){int n=std::min(block,total-pos);juce::MidiBuffer midi;if(at>=pos&&at<pos+n)midi.addEvent(juce::MidiMessage::noteOn(1,36,1.f),at-pos);e.process(l.data()+pos,r.data()+pos,n,midi,p);}
 int lat=e.getLatencySamples();if(!warm){cold[group].resize(noteFrames*2);for(int i=0;i<noteFrames;i++){cold[group][i]=l[i];cold[group][noteFrames+i]=r[i];}}
 double peak=0;int peakAt=0;for(int i=0;i<1440;i++){double v=std::max(std::abs(l[at+lat+i]),std::abs(r[at+lat+i]));if(v>peak){peak=v;peakAt=i;}}
 int on60=-1,on40=-1;double diff=0;for(int i=0;i<noteFrames-lat;i++){double v=std::max(std::abs(l[at+lat+i]),std::abs(r[at+lat+i]));if(on60<0&&v>peak*.001)on60=i;if(on40<0&&v>peak*.01)on40=i;if(warm)diff=std::max({diff,std::abs((double)l[at+i]-cold[group][i]),std::abs((double)r[at+i]-cold[group][noteFrames+i])});}
 std::lock_guard<std::mutex>guard(lock);std::cout<<presets[(size_t)ix].name<<','<<scene<<','<<p.oversampleIndex<<','<<p.attackMs<<','<<p.stereoDelayMs<<','<<p.width<<','<<p.glideMs<<','<<lat<<','<<on60/48.<<','<<on40/48.<<','<<peakAt/48.<<','<<peak<<','<<diff<<'\n';
 }}};std::vector<std::thread>threads;for(int i=0;i<4;i++)threads.emplace_back(work);for(auto&t:threads)t.join();}
