#include "DSP/KickEngine.h"
#include "Presets.h"
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <vector>
#include <cmath>
using namespace pk;
struct Render { std::vector<float> l,r; int before,after; };
Render run(KickParams p,double sr,int block,int second,float velocity=1,bool omitFirst=false,double seconds=.5){
 dsp::KickEngine e;e.prepare(sr,block); Render a; a.before=e.getLatencySamples();int n=(int)(seconds*sr);a.l.resize(n);a.r.resize(n);
 for(int pos=0;pos<n;pos+=block){int len=std::min(block,n-pos);juce::MidiBuffer m;if(pos==0&&!omitFirst)m.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);
 if(second>=pos&&second<pos+len)m.addEvent(juce::MidiMessage::noteOn(1,60,velocity),second-pos);
 e.process(a.l.data()+pos,a.r.data()+pos,len,m,0,p,150,false);}
 a.after=e.getLatencySamples();return a;
}
int onsetDiff(const Render&a,const Render&b,int from,double tol){for(int i=from;i<(int)a.l.size();++i)if(std::max(std::abs(a.l[i]-b.l[i]),std::abs(a.r[i]-b.r[i]))>tol)return i-from;return -1;}
int main(int argc,char**argv){
 if(argc>1&&std::string(argv[1])=="peaks"){
  std::puts("preset,case,sr,velocity,bpm,step,pre_ceiling_dbfs");
  for(auto& pr:factoryPresets()){
   auto p=paramsFromSnapshot(pr.values);p.outputDb-=24.f;
   for(float v:{.25f,.5f,.7f,.85f,1.f}){auto a=run(p,48000,127,0,v,true,1.0);double pk=0;for(size_t i=0;i<a.l.size();++i)pk=std::max({pk,(double)std::abs(a.l[i]),(double)std::abs(a.r[i])});std::printf("%s,single,48000,%.2f,150,0,%.9f\n",pr.name.toRawUTF8(),v,20*std::log10(std::max(pk,1e-15))+24);}
   for(int bpm:{90,150,210,300}){
    int sr=48000,step=(int)std::llround(sr*60./bpm/8),n=sr;dsp::KickEngine e;e.prepare(sr,127);std::vector<float>l(n),r(n);int hit=0;
    for(int pos=0;pos<n;pos+=127){int len=std::min(127,n-pos);juce::MidiBuffer m;for(int t=pos;t<pos+len;++t)if(t%step==0)m.addEvent(juce::MidiMessage::noteOn(1,60,hit++%2==0?1.f:.8f),t-pos);e.process(l.data()+pos,r.data()+pos,len,m,0,p,bpm,false);}
    double pk=0;for(int i=0;i<n;++i)pk=std::max({pk,(double)std::abs(l[i]),(double)std::abs(r[i])});std::printf("%s,accented32,48000,0.80,%d,%d,%.9f\n",pr.name.toRawUTF8(),bpm,step,20*std::log10(std::max(pk,1e-15))+24);
   }
  }
  return 0;
 }
 auto p=paramsFromSnapshot(factoryPresets()[0].values);p.rumble=0;p.outputDb=-12;p.decay=.4f;p.drive=0;p.character=0;p.bodyTone=1;p.click=0;p.width=0;p.velSens=1;p.pitchFall=0;p.pitchDepth=0;p.punch=0;
 std::puts("TIMING sr,block,os,reported_before,actual_after,isolated_at_note37_tol1e-7,sounding_new_only_tol1e-7,isolated_note5000_tol1e-7,handoff_extra_samples,shift_max_abs_error");
 for(double sr:{44100.,48000.,96000.})for(int block:{32,127,512,1024})for(int os=0;os<3;++os){
 p.oversampleIndex=os;int second=(int)(.05*sr)+37;auto muted=run(p,sr,block,second,1.0f/127.0f);auto loud=run(p,sr,block,second,1.f);auto isolated=run(p,sr,block,second,1.f,true);auto silence=run(p,sr,block,-1,1.f,true);auto at37=run(p,sr,block,37,1.f,true);auto at0=run(p,sr,block,0,1.f,true);
 int solo=onsetDiff(isolated,silence,second,1e-7),on=onsetDiff(loud,muted,second,1e-7);double err=0;for(int i=37;i<(int)at37.l.size();++i)err=std::max(err,(double)std::abs(at37.l[i]-at0.l[i-37]));
 std::printf("%.0f,%d,%d,%d,%d,%d,%d,%d,%d,%.12g\n",sr,block,2<<os,at37.before,at37.after,onsetDiff(at37,silence,37,1e-7),on,solo,on-solo,err);
 }
 std::puts("STEREO impulse mid identity maxabs");for(double width:{0.,.5,1.})for(double delay:{0.,5.,15.}){dsp::StereoEngine e;e.prepare(48000);e.setTargets(width,delay,500,true);double err=0;for(int i=0;i<2400;++i){double l,r,x=i==0?1.:0.;e.tick(x,l,r);err=std::max(err,std::abs((l+r)*.5-x));}std::printf("width %.1f delay %.1f error %.17g\n",width,delay,err);}
 return 0;
}
