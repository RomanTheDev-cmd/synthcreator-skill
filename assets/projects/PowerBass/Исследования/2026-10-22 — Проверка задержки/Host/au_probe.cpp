#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Offline only: no device, AudioOutputUnitStart, project files or preset writes.
static void check(OSStatus s, const char* what) {
    if (s) throw std::runtime_error(std::string(what) + ": " + std::to_string(s));
}
struct AU {
    AudioUnit u = nullptr;
    AU(OSType code, double sr) {
        AudioComponentDescription d { kAudioUnitType_MusicDevice, code, 'Mddy', 0, 0 };
        auto c = AudioComponentFindNext(nullptr, &d);
        if (!c) throw std::runtime_error("component not found");
        check(AudioComponentInstanceNew(c, &u), "instantiate");
        AudioStreamBasicDescription f {};
        f.mSampleRate=sr; f.mFormatID=kAudioFormatLinearPCM;
        f.mFormatFlags=kAudioFormatFlagsNativeFloatPacked|kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket=4; f.mFramesPerPacket=1; f.mBytesPerFrame=4;
        f.mChannelsPerFrame=2; f.mBitsPerChannel=32;
        check(AudioUnitSetProperty(u,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&f,sizeof(f)), "format");
        UInt32 max=2048;
        check(AudioUnitSetProperty(u,kAudioUnitProperty_MaximumFramesPerSlice,kAudioUnitScope_Global,0,&max,sizeof(max)), "max frames");
        check(AudioUnitInitialize(u), "initialize");
    }
    ~AU() { if(u) { AudioUnitUninitialize(u); AudioComponentInstanceDispose(u); } }
    double latency() const {
        Float64 x=-1; UInt32 n=sizeof(x);
        check(AudioUnitGetProperty(u,kAudioUnitProperty_Latency,kAudioUnitScope_Global,0,&x,&n), "latency"); return x;
    }
};
struct Run { std::vector<float> l,r; double initialLatency=0, finalLatency=0, peak=0; int first=-1; };
static Run render(OSType code, double sr, int block, int shift) {
    AU au(code,sr);
    if(const char* os=std::getenv("AU_PROBE_OS"))
        check(AudioUnitSetParameter(au.u,394773374,kAudioUnitScope_Global,0,(float)std::atoi(os),0),"oversampling parameter");
    Run r; r.initialLatency=au.latency();
    const int warmup=std::getenv("AU_PROBE_WARMUP") ? (int)sr : 0;
    const int total=(int)sr+warmup; r.l.resize(total);r.r.resize(total);
    struct Event { int t; int status; int velocity; };
    // Includes sub-block on/off offsets and retrigger overlap. All at absolute sample times.
    const Event events[] {{warmup+shift,0x90,100},{warmup+(int)(sr*.12)+shift,0x80,0},
                         {warmup+(int)(sr*.16)+shift,0x90,72},{warmup+(int)(sr*.31)+shift,0x80,0}};
    struct { UInt32 count; AudioBuffer b[2]; } abl;
    for(int pos=0;pos<total;pos+=block) {
        int n=std::min(block,total-pos);
        for(auto e:events) if(e.t>=pos && e.t<pos+n)
            check(MusicDeviceMIDIEvent(au.u,e.status,36,e.velocity,(UInt32)(e.t-pos)),"MIDI");
        abl.count=2;abl.b[0]={1,(UInt32)n*4,r.l.data()+pos};abl.b[1]={1,(UInt32)n*4,r.r.data()+pos};
        AudioTimeStamp ts {};ts.mFlags=kAudioTimeStampSampleTimeValid;ts.mSampleTime=pos;
        AudioUnitRenderActionFlags flags=0;
        check(AudioUnitRender(au.u,&flags,&ts,0,n,reinterpret_cast<AudioBufferList*>(&abl)),"render");
    }
    r.finalLatency=au.latency();
    for(int i=0;i<total;i++) {
        if(!std::isfinite(r.l[i]) || !std::isfinite(r.r[i])) throw std::runtime_error("nonfinite audio");
        auto a=std::max(std::abs(r.l[i]),std::abs(r.r[i]));r.peak=std::max(r.peak,(double)a);
        if(r.first<0 && a>1e-7)r.first=i;
    }
    return r;
}
static double difference(const Run&a,const Run&b,int shift=0) {
    double d=0;
    for(size_t i=0;i+shift<a.l.size();i++)
        d=std::max({d,std::abs((double)a.l[i]-b.l[i+shift]),std::abs((double)a.r[i]-b.r[i+shift])});
    return d;
}
int main() {
    int fail=0;
    try {
        std::cout << std::setprecision(12);
        for(auto pr: {std::pair<const char*,OSType>{"PowerKick",'Pwkk'},{"PowerBass",'Pwbs'}})
            for(double sr:{44100.,48000.,96000.}) {
                auto ref=render(pr.second,sr,512,0);
                std::cout << "{\"plugin\":\""<<pr.first<<"\",\"sr\":"<<sr<<",\"block\":512,\"latency_initial_samples\":"<<ref.initialLatency*sr<<",\"latency_final_samples\":"<<ref.finalLatency*sr<<",\"first_above_1e7\":"<<ref.first<<",\"peak\":"<<ref.peak<<"}\n";
                for(int bs:{16,64,128,1024}) {
                    auto r=render(pr.second,sr,bs,0);double diff=difference(ref,r);bool ok=diff<1e-5;
                    fail+=!ok;
                    std::cout << "{\"plugin\":\""<<pr.first<<"\",\"sr\":"<<sr<<",\"block\":"<<bs<<",\"max_abs_diff_from_512\":"<<diff<<",\"pass\":"<<(ok?"true":"false")<<"}\n";
                }
                auto shifted=render(pr.second,sr,512,17);double diff=difference(ref,shifted,17);bool ok=diff<1e-5 && shifted.first==ref.first+17;
                fail+=!ok;
                std::cout << "{\"plugin\":\""<<pr.first<<"\",\"sr\":"<<sr<<",\"MIDI_shift\":17,\"first\":"<<shifted.first<<",\"max_abs_diff_aligned\":"<<diff<<",\"pass\":"<<(ok?"true":"false")<<"}\n";
            }
    } catch(const std::exception&e) {std::cerr<<e.what()<<"\n";return 2;}
    std::cout << "{\"failed_comparisons\":"<<fail<<"}\n";
    return fail?1:0;
}
