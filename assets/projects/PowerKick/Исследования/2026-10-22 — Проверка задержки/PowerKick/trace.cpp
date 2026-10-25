#include "TracedKickEngine.h"
#include "Presets.h"
#include <iostream>
using namespace pk;
int main(int argc,char**argv){if(argc<5)return 2;KickParams p;bool found=false;for(auto&pr:factoryPresets())if(pr.name==argv[1]){p=paramsFromSnapshot(pr.values);found=true;}if(!found)return 3;
 int step=std::atoi(argv[2]);float velocity=(float)std::atof(argv[3]);p.outputDb-=24;for(int i=5;i+1<argc;i+=2){std::string k=argv[i];float v=(float)std::atof(argv[i+1]);if(k=="character")p.character=v;if(k=="drive")p.drive=v;if(k=="click")p.click=v;if(k=="rumble")p.rumble=v;if(k=="width")p.width=v;}
 constexpr int n=48000,block=127;std::vector<std::array<double,18>> records(n);dsp::traceRecords=&records;dsp::TracedKickEngine e;e.prepare(48000,block);std::vector<float>l(n),r(n);
 for(int pos=0;pos<n;pos+=block){int len=std::min(block,n-pos);juce::MidiBuffer midi;for(int i=0;i<len;++i)if(pos+i==0||(step>0&&(pos+i)%step==0))midi.addEvent(juce::MidiMessage::noteOn(1,60,velocity),i);dsp::traceBase=pos;e.process(l.data()+pos,r.data()+pos,len,midi,0,p,210,false);}
 std::ofstream f(argv[4],std::ios::binary);f.write(reinterpret_cast<const char*>(records.data()),n*sizeof(records[0]));std::cout<<"records "<<n<<" doubles_per_record 18\n";return f?0:4;}
