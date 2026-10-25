#include "BassEngineProbe.h"
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cassert>
using namespace pb;
using C = std::complex<double>;
C response(const std::vector<double>& h,double f,double sr) { C z=0; for(size_t n=0;n<h.size();++n) z+=h[n]*std::polar(1.0,-2*dsp::pi*f*n/sr); return z; }
double gd(C a,C b) { return -std::arg(b*std::conj(a))/(2*dsp::pi*.2)*1000; }
int main(int argc,char**argv) {
 const bool check=false;double auditWidth=argc>1?std::stod(argv[1]):0;int fails=0;
 std::vector<double> unit(200);unit[48]=1;
 if(std::abs(gd(response(unit,60-.1,48000),response(unit,60+.1,48000))-1)>1e-9)return 2;
 unit[48]=0;unit[0]=1;
 if(std::abs(gd(response(unit,60-.1,48000),response(unit,60+.1,48000)))>1e-9)return 3;
 std::cout<<std::setprecision(12)<<"stage,sr,split,frequency,group_delay_ms,magnitude_db\n";
 for(double sr:{44100.,48000.,96000.}) for(float split:{150.f,300.f,1000.f}) {
  dsp::BassEngine e;e.prepare(sr,512);SynthParams p;p.splitHz=split;e.snapTo(p);
  const double dsr=e.osRate;
  std::vector<double> hd((size_t)(dsr*.35)),hs((size_t)(sr*.35));
  // Distortion runs at osRate; stereo runs at base sr. Never clock the XO at the wrong rate.
  for(size_t n=0;n<hd.size();++n)hd[n]=e.distort(n==0?.001:0,1,1,0)/.001;
  for(size_t n=0;n<hs.size();++n){double l,r;e.stereo(n==0?.001:0,auditWidth,0,l,r);hs[n]=(l+r)/.002;}
  if(check) {
   double ed=0,es=0;
   for(size_t n=0;n<hd.size();++n)ed=std::max(ed,std::abs(hd[n]-(n==0?1.:0.)));
   for(size_t n=0;n<hs.size();++n)es=std::max(es,std::abs(hs[n]-(n==0?1.:0.)));
   std::cerr<<"dry_distortion_identity sr="<<sr<<" split="<<split<<" error="<<ed<<"\n";
   std::cerr<<"zero_width_identity sr="<<sr<<" split="<<split<<" error="<<es<<"\n";
   if(ed>1e-9)++fails;if(es>1e-9)++fails;
   continue;
  }
  for(int stage=0;stage<3;++stage)for(double f:{40.,60.,90.,120.,300.,1000.,4000.}) {
   auto H=[&](double hz){C z=1.;if(stage!=1)z*=response(hd,hz,dsr);if(stage!=0)z*=response(hs,hz,sr);return z;};
   auto a=H(f-.1),b=H(f+.1),c=H(f);
   std::cout<<(stage==0?"distortion_dry":stage==1?"stereo_centre":"dist_dry_and_stereo_centre")<<","<<sr<<","<<split<<","<<f<<","<<gd(a,b)<<","<<20*std::log10(std::abs(c))<<"\n";
  }
 }
 return fails?1:0;
}
