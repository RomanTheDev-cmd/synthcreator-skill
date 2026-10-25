#define main wrapper_full_main
#include "/Users/roman/Library/Mobile Documents/com~apple~CloudDocs/Главная рабочая папка ИИ/Проекты/PowerKick/Tests/WrapperTests.cpp"
#undef main

// Darwin malloc's exported diagnostic callback. Counts allocations/reallocations on this test
// thread only; availability and AudioBuffer malloc coverage are positively controlled below.
extern "C" {
using MallocLogger = void(uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);
extern MallocLogger* malloc_logger;
}
thread_local bool trackHeap = false;
thread_local int heapCount = 0;
void heapLogger(uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t) {
 if(trackHeap && (type & 2u)) ++heapCount;
}
int main() {
 juce::ScopedJuceInitialiser_GUI init;
 auto* previousLogger = malloc_logger; malloc_logger = heapLogger;
 heapCount=0; trackHeap=true;
 { juce::AudioBuffer<float> control(1,32768); control.setSample(0,0,1.f); }
 trackHeap=false;
 expect(heapCount>0,"malloc callback detects JUCE AudioBuffer allocation ("+juce::String(heapCount)+")");
 for(int prepared: {32,512}) {
  PowerKickProcessor mono,stereo;
  juce::AudioProcessor::BusesLayout layout;layout.outputBuses.add(juce::AudioChannelSet::mono());
  expect(mono.setBusesLayout(layout),"oversized mono bus accepted");
  for(auto* p:{&mono,&stereo}) {
   setPlain(*p,pk::id::width,80);setPlain(*p,pk::id::delay,12);
   setPlain(*p,pk::id::modDrive,1);setPlain(*p,pk::id::choke,1);
   p->prepareToPlay(48000,prepared);
  }
  stereo.monoCheck=true;
  double error=0;int allocated=0,heapAllocated=0;
  for(int b=0;b<4;++b) {
   juce::AudioBuffer<float> a(1,4096),s(2,4096);a.clear();s.clear();
   juce::MidiBuffer ma,ms;ma.ensureSize(8192);ms.ensureSize(8192);
   for(int at:{0,513,1673,1701,3070}) ma.addEvent(juce::MidiMessage::noteOn(1,60,at%2?0.7f:1.f),at);
   ma.addEvent(juce::MidiMessage::noteOff(1,60),1699);
   ma.addEvent(juce::MidiMessage::allNotesOff(1),3700);
   for(int at=73;at<4096;at+=137)ma.addEvent(juce::MidiMessage::controllerEvent(1,1,at%128),at);
   ms=ma;
   allocCount=0;heapCount=0;trackAllocs=true;trackHeap=true;mono.processBlock(a,ma);trackHeap=false;trackAllocs=false;allocated+=allocCount.load();heapAllocated+=heapCount;
   stereo.processBlock(s,ms);
   for(int i=0;i<4096;++i)error=std::max(error,std::abs((double)a.getSample(0,i)-s.getSample(0,i)));
  }
  expect(heapAllocated==0,"malloc/realloc callback: 4096-sample mono blocks allocate zero times, prepared "+juce::String(prepared)+", got "+juce::String(heapAllocated));
  expect(allocated==0,"4096-sample mono blocks allocate zero times, prepared "+juce::String(prepared));
  expect(error<1e-6,"oversized mono preserves stereo-folded MIDI/audio, prepared "+juce::String(prepared)+", error "+juce::String(error,9));
 }
 malloc_logger=previousLogger;
 std::cout<<(failures?"FAILED ":"PASS ")<<checks-failures<<"/"<<checks<<" checks\n";
 return failures?1:0;
}
