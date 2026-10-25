#include "DSP/KickEngine.h"
#include <iostream>
#include <vector>
#include <cmath>
int main(){
 pk::KickParams p; p.punch=0; p.pitchDepth=0; p.pitchFall=0; p.drive=0; p.character=0; p.click=0; p.rumble=0; p.width=0; p.bodyTone=1; p.outputDb=-12; p.choke=true;
 for(int kind=0;kind<6;++kind) for(int cancel: {1738,1755,1761}){
  pk::dsp::KickEngine e;e.prepare(48000,127);std::vector<float> l(4096),r(4096);juce::MidiBuffer m;
  m.addEvent(juce::MidiMessage::noteOn(1,60,1.0f),0);m.addEvent(juce::MidiMessage::noteOn(1,60,1.0f),1714);
  if(kind==0)m.addEvent(juce::MidiMessage::noteOff(1,60),cancel);
  if(kind==1)m.addEvent(juce::MidiMessage::allNotesOff(1),cancel);
  if(kind==2)m.addEvent(juce::MidiMessage::allSoundOff(1),cancel);
  if(kind==3)m.addEvent(juce::MidiMessage::noteOn(1,60,1.f),cancel);
  if(kind==4){m.addEvent(juce::MidiMessage::noteOff(1,60),cancel);m.addEvent(juce::MidiMessage::noteOn(1,60,1.f),cancel);}
  if(kind==5){m.addEvent(juce::MidiMessage::noteOn(1,60,1.f),cancel);m.addEvent(juce::MidiMessage::noteOff(1,60),cancel);}
  e.process(l.data(),r.data(),4096,m,0,p,140,false);
  double jump=0;int at=0;for(int i=1714;i<1800;++i)if(std::abs(l[i]-l[i-1])>jump){jump=std::abs(l[i]-l[i-1]);at=i;}
  std::cout<<"kind "<<kind<<" cancel "<<cancel<<" jump "<<jump<<" at "<<at<<" prev "<<l[at-1]<<" next "<<l[at]<<"\n";
 }
}
