#include "KickEngineTiming.h"
#include "Presets.h"
#include <iostream>
#include <iomanip>
#include <atomic>
#include <thread>
#include <mutex>
using namespace pk;
int main(){
 constexpr int sr=48000,block=512;const auto& presets=factoryPresets();std::atomic<int>next{0};std::mutex lock;
 std::cout<<"preset,os,scenario,hit,requested,voice_start,extra_start_samples,reported_latency,onset_60db_ms,onset_40db_ms,peak30_ms,peak30,attack_max_error_vs_cold\n";
 auto worker=[&]{for(int ix;(ix=next++)<(int)presets.size();){auto p=paramsFromSnapshot(presets[(size_t)ix].values);
 for(int os:{0,1,2}){p.oversampleIndex=os;std::vector<float> cold;
 for(int scene:{0,1,2}){std::vector<int> notes=scene==0?std::vector<int>{0}:scene==1?std::vector<int>{48000}:std::vector<int>{0,6000,12000};
 int total=notes.back()+4800;std::vector<float> l(total),r(total);dsp::KickEngine e;e.prepare(sr,block,os);e.auditStarts.reserve(16);
 for(int pos=0;pos<total;pos+=block){int n=std::min(block,total-pos);juce::MidiBuffer midi;for(auto at:notes)if(at>=pos&&at<pos+n)midi.addEvent(juce::MidiMessage::noteOn(1,60,1.f),at-pos);e.auditBase=pos;e.process(l.data()+pos,r.data()+pos,n,midi,0,p,140,false);}
 int lat=e.getLatencySamples();if(scene==0)cold=l;
 for(size_t hit=0;hit<notes.size();hit++){int at=notes[hit];double peak=0;int peakAt=0;for(int j=0;j<1440;j++){double v=std::max(std::abs(l[at+lat+j]),std::abs(r[at+lat+j]));if(v>peak){peak=v;peakAt=j;}}
 int on60=-1,on40=-1;double err=0;for(int j=0;j<4000;j++){double v=std::max(std::abs(l[at+lat+j]),std::abs(r[at+lat+j]));if(on60<0&&v>peak*.001)on60=j;if(on40<0&&v>peak*.01)on40=j;if(scene==1)err=std::max(err,std::abs((double)l[at+lat+j]-cold[lat+j]));}
 long long start=hit<e.auditStarts.size()?e.auditStarts[hit]:-9999;
 std::lock_guard<std::mutex> guard(lock);std::cout<<presets[(size_t)ix].name<<','<<os<<','<<scene<<','<<hit<<','<<at<<','<<start<<','<<start-at<<','<<lat<<','<<on60/48.<<','<<on40/48.<<','<<peakAt/48.<<','<<peak<<','<<err<<'\n';}
 }}}};std::vector<std::thread> threads;for(int i=0;i<4;i++)threads.emplace_back(worker);for(auto&t:threads)t.join();}
