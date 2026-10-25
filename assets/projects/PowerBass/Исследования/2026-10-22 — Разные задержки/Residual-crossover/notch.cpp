#include "BassEngineResidual.h"
#include "Presets.h"
#include <iostream>
#include <fstream>
#include <complex>
#include <atomic>
#include <thread>
#include <mutex>
using namespace pb;
std::complex<double> measure(int mode,double hz,double amp,double drive,double mix,double tone,int type){
 SynthParams p;p.distType=(DistType)type;p.oversampleIndex=1;
 dsp::BassEngine e;e.auditMode=mode;e.prepare(48000,512,p);
 const double sr=192000;std::complex<double> z=0;
 for(int i=0;i<384000;i++){double phase=2*dsp::pi*hz*i/sr;double y=e.auditDistort(amp*std::cos(phase),drive,mix,tone);if(i>=192000)z+=y*std::complex<double>(std::cos(phase),-std::sin(phase));}
 return z*(2./192000/amp);
}
int main(int argc,char**argv){
 std::cout<<"stage,mode,hz,amplitude,drive,mix,tone,type,gain_db,phase,group_delay_ms\n";
 for(int mode:{0,2})for(double amp:{.0001,.1,1.})for(double drive:{3.529411764705882})for(double mix:{.5,1.})for(double hz:{80.,100.,110.,120.,130.,140.,160.}) {
  const auto z=measure(mode,hz,amp,drive,mix,22000.,1);
  const auto za=measure(mode,hz-.5,amp,drive,mix,22000.,1),zb=measure(mode,hz+.5,amp,drive,mix,22000.,1);
  const double gd=-std::arg(zb/std::max(1.e-20,std::abs(za))*std::conj(za/std::max(1.e-20,std::abs(za))))/(2*dsp::pi)*1000;
  std::cout<<"dist,"<<mode<<','<<hz<<','<<amp<<','<<drive<<','<<mix<<",22000,1,"<<20*std::log10(std::abs(z))<<','<<std::arg(z)<<','<<gd<<'\n';
 }
 return 0;
}