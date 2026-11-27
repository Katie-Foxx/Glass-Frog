#include <cstdio>
#include <random>
#include "Source/DSP/Denoise.h"
int main(){
  const double sr=48000; std::mt19937 rg(1); std::normal_distribution<double> nd(0,1);
  // learn at level 0.01 white noise
  gf::Denoiser d; d.prepare(sr); gf::Settings s; d.setSettings(s);
  auto feed=[&](gf::Denoiser&dd,double lvl,double secs,bool speech){ for(int i=0;i<(int)(sr*secs);i++){ double t=i/sr; double v=lvl*nd(rg); if(speech && std::fmod(t,1.0)<0.5) v+=0.2*std::sin(6.283*220*t)*std::sin(6.283*3*t); double a=v,b=v; dd.processFrame(a,b);} };
  d.startLearn(); feed(d,0.01,1.0,false); d.stopLearn();
  std::printf("learned frames %d has %d\n",d.learnedFrames(),(int)d.hasProfile());
  feed(d,0.01,4.0,true);  double o0=d.floorOffsetDb();
  gf::Denoiser d2; d2.prepare(sr); d2.setSettings(s); auto prof=d.profile(); d2.setProfile(prof);
  feed(d2,0.02,5.0,true); double o6=d2.floorOffsetDb();   // +6 dB louder noise
  gf::Denoiser d3; d3.prepare(sr); d3.setSettings(s); d3.setProfile(prof);
  feed(d3,0.0316,5.0,true); double o10=d3.floorOffsetDb();
  std::printf("same level: %.2f dB | +6 dB: %.2f dB | +10 dB: %.2f dB\n",o0,o6,o10);
  std::printf("diff6 %.2f (want 6) diff10 %.2f (want 10)\n",o6-o0,o10-o0);
  // convert 48k N=2048 -> 96k N=4096 -> back
  auto c=gf::Denoiser::convertProfile(prof,2048,48000,4096,96000,0.0); auto b=gf::Denoiser::convertProfile(c,4096,96000,2048,48000,0.0);
  double md=0; for(size_t i=1;i<prof.size()-1;i++) md=std::max(md,(double)std::abs(10*std::log10(b[i]/prof[i]))); std::printf("round trip max dB err %.2f, size %zu->%zu\n",md,prof.size(),c.size());
  auto g6=gf::Denoiser::convertProfile(prof,2048,48000,2048,48000,6.0); std::printf("trim +6: %.2f dB\n",10*std::log10(g6[100]/prof[100]));
}
