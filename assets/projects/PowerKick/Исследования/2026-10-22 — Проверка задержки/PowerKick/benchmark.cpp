#include <cstdlib>
#include <new>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <vector>
thread_local bool trackNews=false;thread_local size_t trackedNews=0;
void* operator new(std::size_t n){if(trackNews)++trackedNews;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#ifdef BEFORE
#include "BenchmarkBaseline.h"
using Engine=pk::dsp::BaselineKickEngine;
#else
#include "DSP/KickEngine.h"
using Engine=pk::dsp::KickEngine;
#endif
int main(int argc,char**argv){if(argc<2)return 2;std::ofstream raw(argv[1]);raw<<"sr,block,call,has_note,time_ns,new_count\n";std::puts("sr,block,calls,notes,median_ns,p99_ns,p999_ns,max_ns,block_budget_ns,over_budget,new_calls");
 for(double sr:{48000.,96000.,192000.})for(int block:{32,128}){
  Engine e;e.prepare(sr,block);pk::KickParams p;p.drive=.6f;p.character=.7f;p.body=.7f;p.decay=.6f;p.rumble=0;p.width=30;p.outputDb=-12;
  std::vector<float>l(block),r(block);juce::MidiBuffer m;m.ensureSize(4096);const int step=(int)std::llround(sr*.025),calls=20000;std::vector<long long> times;times.reserve(calls);int notes=0;size_t news=0;int over=0;
  for(int c=-1000;c<calls;++c){int pos=(c+1000)*block;m.clear();bool note=false;for(int i=0;i<block;++i)if((pos+i)%step==0){m.addEvent(juce::MidiMessage::noteOn(1,60,1.f),i);note=true;}
   trackedNews=0;trackNews=true;auto a=std::chrono::steady_clock::now();e.process(l.data(),r.data(),block,m,0,p,300,false);auto b=std::chrono::steady_clock::now();trackNews=false;
   if(c>=0){auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();times.push_back(ns);raw<<sr<<','<<block<<','<<c<<','<<note<<','<<ns<<','<<trackedNews<<'\n';news+=trackedNews;if(note)++notes;if(ns>1e9*block/sr)++over;}
  }
  std::sort(times.begin(),times.end());std::printf("%.0f,%d,%d,%d,%lld,%lld,%lld,%lld,%.0f,%d,%zu\n",sr,block,calls,notes,times[calls/2],times[(int)(calls*.99)],times[(int)(calls*.999)],times.back(),1e9*block/sr,over,news);
 }
 return raw?0:3;
}
