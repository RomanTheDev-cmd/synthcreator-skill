#include "DSP/KickEngine.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <vector>
#include <algorithm>
int main(int argc, char** argv) {
 if (argc != 2) return 2;
 for(int mode=0;mode<5;++mode) {
  pk::KickParams p; p.driveMode=pk::DriveMode::industrial; p.drive=.6f; p.outputDb=-24; p.modDrive=mode>=3?1.f:0.f;
  std::vector<double> times;
  std::vector<float> out(960000);
  for(int rep=0;rep<7;++rep) {
   pk::dsp::KickEngine e; e.prepare(48000,512);
   std::vector<juce::MidiBuffer> midi(938);
   for(int b=0;b<938;++b) {
    int start=b*512, n=std::min(512,480000-start);
    for(int j=0;j<n;++j) {
     if((start+j)%2400==0) midi[(size_t)b].addEvent(juce::MidiMessage::noteOn(1,60,(start+j)%4800==0?1.f:.7f),j);
     if(mode && (mode==4 ? j==73||j==217||j==407 : j%4==0))
      midi[(size_t)b].addEvent(juce::MidiMessage::controllerEvent(1,mode==2?7:1,((start+j)/4)%128),j);
    }
   }
   auto t=std::chrono::steady_clock::now();
   for(int b=0;b<938;++b) {
    int start=b*512,n=std::min(512,480000-start);
    e.process(out.data()+start,out.data()+480000+start,n,midi[(size_t)b],0,p,140,false);
   }
   times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());
  }
  std::sort(times.begin(),times.end());
  std::cout<<mode<<","<<times[3]<<","<<times.front()<<","<<times.back()<<"\n";
  std::string path=std::string(argv[1])+"-"+std::to_string(mode)+".f32";
  FILE* f=std::fopen(path.c_str(),"wb"); if(!f) return 3;
  if(std::fwrite(out.data(),sizeof(float),out.size(),f)!=out.size()) return 4;
  if(std::fclose(f)) return 5;
 }
}
