#include "raster_core.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile uint64_t sinkBenchmark=0;
int main(){
 Settings s{};s.startX=128;s.startY=512;s.endX=1728;s.endY=512;
 const DirectRamp direct{{.15f,.7f,.25f,.2f},
                         {.9f,.2f,.85f,.95f},
                         {.99f,.12f,.47f,.75f},.43f,3};
 Gradient g(s,{{0,direct.first},{.43f,direct.middle},
               {1,direct.last}});
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 constexpr int W=1920,H=1080,BPP=16;
 const size_t bytes=(size_t)W*H*BPP;
 std::vector<uint8_t> source(bytes),out(bytes);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const float a=((x*11+y*17)%19)?1.f:.5f;
  Pixel32 v{a,a*.4f,a*.2f,a*.8f};
  std::memcpy(source.data()+((size_t)y*W+x)*BPP,&v,BPP);
 }
 Buffer input{source.data(),W,H,W*BPP,0,0};
 WritableBuffer dest{out.data(),W,H,W*BPP,0,0};
 for(int shape=0;shape<4;shape++){
  s.shape=(Shape)shape;
  for(int config=0;config<3;config++){
   if(shape==3&&config==0)continue;
   blend::Config cfg{};
   if(config==0)cfg={blend::Mode::Replace,blend::Mode::Replace,0};
   if(config==1)cfg={blend::Mode::Replace,blend::Mode::Multiply,0};
   if(config==2)cfg={blend::Mode::Normal,blend::Mode::Multiply,0};
   const Buffer in=config==0?Buffer{}:input;
   std::vector<double> median;
   for(int variant=0;variant<2;variant++){
    const DirectRamp* pick=variant==0?nullptr:&direct;
    for(int j=0;j<2;j++)render(in,dest,Depth::F32,s,ramp,nullptr,nullptr,cfg,pick);
    std::vector<double> times;
    for(int j=0;j<7;j++){
     auto t0=std::chrono::steady_clock::now();
     if(!render(in,dest,Depth::F32,s,ramp,nullptr,nullptr,cfg,pick))return 3;
     auto t1=std::chrono::steady_clock::now();
     times.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());
    }
    std::sort(times.begin(),times.end());
    median.push_back(times[3]);
    sinkBenchmark+=out[0]+out[out.size()/2]+out.back();
   }
   const char* names[]={"Linear","Radial","Angle","Mapping"};
   std::cout<<names[shape]<<" cfg "<<config
    <<" LUT_ms "<<median[0]<<" DIRECT_ms "<<median[1]
    <<" SPEEDUP "<<median[0]/median[1]<<"x\n";
  }
 }
 std::cout<<"sink "<<sinkBenchmark<<"\n";
}
