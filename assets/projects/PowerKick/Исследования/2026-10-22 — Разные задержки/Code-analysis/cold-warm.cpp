#include "DSP/KickEngine.h"
#include "Presets.h"
#include "SnapScoopHypothesis.h"
#include "SnapBothHypothesis.h"
#include <iostream>
#include <iomanip>
#include <vector>
template<class Engine> std::vector<float> run(pk::KickParams p,int idle) {
 Engine e;e.prepare(48000,512,p.oversampleIndex);std::vector<float> l(48000),r(48000);juce::MidiBuffer none;
 for(int pos=0;pos<idle;pos+=512)e.process(l.data(),r.data(),std::min(512,idle-pos),none,0,p,140,false);
 for(int pos=0;pos<48000;pos+=512){int n=std::min(512,48000-pos);juce::MidiBuffer m;if(!pos)m.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);e.process(l.data()+pos,r.data()+pos,n,m,0,p,140,true);}return l;
}
void report(const char* label,const std::vector<float>&a,const std::vector<float>&b) {
 double diff=0,e0=0,e1=0;int first0=-1,first1=-1;for(int i=0;i<48000;++i){diff=std::max(diff,std::abs((double)a[(size_t)i]-b[(size_t)i]));if(first0<0&&std::abs(a[(size_t)i])>1e-4)first0=i;if(first1<0&&std::abs(b[(size_t)i])>1e-4)first1=i;if(i<480){e0+=a[(size_t)i]*a[(size_t)i];e1+=b[(size_t)i]*b[(size_t)i];}}
 std::cout<<label<<"\t"<<std::setprecision(12)<<diff<<"\t"<<first0<<"\t"<<first1<<"\t"<<10*std::log10((e1+1e-30)/(e0+1e-30))<<"\n";
}
int main(){std::cout<<"scenario\tmax_abs_diff\tonset_cold\tonset_idle\tfirst10ms_warm_minus_cold_db\n";
 for(const char* name:{"Hard Techno Core","Classic Punch","Rawstyle Screech"}){
  pk::KickParams p;bool found=false;for(const auto&pr:pk::factoryPresets())if(pr.name==name){p=pk::paramsFromSnapshot(pr.values);found=true;break;}if(!found)return 2;
  p.width=0;p.rumble=0;
  auto cold=run<pk::dsp::KickEngine>(p,0),warm=run<pk::dsp::KickEngine>(p,48000);
  report((std::string(name)+" original").c_str(),cold,warm);
  report((std::string(name)+" snap-scoop hypothesis").c_str(),run<pk::snap_scoop::KickEngine>(p,0),run<pk::snap_scoop::KickEngine>(p,48000));
  report((std::string(name)+" snap-both hypothesis").c_str(),run<pk::snap_both::KickEngine>(p,0),run<pk::snap_both::KickEngine>(p,48000));
  auto noBody=p;noBody.bodyTone=1;report((std::string(name)+" snap-scoop bodyTone1 control").c_str(),run<pk::snap_scoop::KickEngine>(noBody,0),run<pk::snap_scoop::KickEngine>(noBody,48000));
  p.drive=0;report((std::string(name)+" drive0 control").c_str(),run<pk::dsp::KickEngine>(p,0),run<pk::dsp::KickEngine>(p,48000));
 }
}
