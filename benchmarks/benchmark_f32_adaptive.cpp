#include "../src/render/cpu/raster_parallel.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile unsigned long long checksum=0;
int main(){
 Settings settings{};
 settings.startX=128;settings.startY=512;
 settings.endX=1728;settings.endY=840;
 const DirectRamp direct{{.14f,.81f,.25f,.25f},
                         {.92f,.17f,.74f,.99f}};
 Gradient g(settings,{{0,direct.first},{1,direct.last}});
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[(size_t)i]=g.sample(i/4096.f);
 constexpr int W=1920,H=1080;
 constexpr int BYTES=W*H*16;
 std::vector<uint8_t> src(BYTES),a(BYTES),b(BYTES);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const float alpha=((x+y)%13==0)?0.38f:1.f;
  Pixel32 p{alpha,.23f*alpha,.79f*alpha,.61f*alpha};
  std::memcpy(src.data()+((size_t)y*W+x)*16,&p,16);
 }
 Buffer in{src.data(),W,H,W*16,0,0};
 WritableBuffer dstA{a.data(),W,H,W*16,0,0},
                dstB{b.data(),W,H,W*16,0,0};
 for(int shape=0;shape<4;shape++){
  settings.shape=(Shape)shape;
  for(int cfg=0;cfg<3;cfg++){
   if(shape==3&&cfg==0)continue;
   const blend::Config mode=cfg==0?
      blend::Config{blend::Mode::Replace,blend::Mode::Replace,0}:
      cfg==1?blend::Config{blend::Mode::Replace,blend::Mode::Multiply,0}:
             blend::Config{blend::Mode::Normal,blend::Mode::Multiply,0};
   const Buffer input=cfg==0?Buffer{}:in;
   auto go=[&](bool parallel){
    return parallel?
      gradation::raster::parallel::render(input,dstB,Depth::F32,
                                         settings,ramp,nullptr,nullptr,mode,&direct):
      gradation::raster::render(input,dstA,Depth::F32,
                               settings,ramp,nullptr,nullptr,mode,&direct);
   };
   if(!go(false)||!go(true)||std::memcmp(a.data(),b.data(),a.size())!=0)
    return 2;
   double median[2]{};
   for(int variant=0;variant<2;variant++){
    for(int i=0;i<2;i++)go(variant==1);
    std::vector<double> times;
    for(int i=0;i<9;i++){
     const auto before=std::chrono::steady_clock::now();
     if(!go(variant==1))return 3;
     const auto after=std::chrono::steady_clock::now();
     times.push_back(std::chrono::duration<double,std::milli>(after-before).count());
    }
    std::sort(times.begin(),times.end());
    median[variant]=times[4];
   }
   checksum+=b[0]+b[b.size()/2];
   const char* shapeName[]={"Linear","Radial","Angle","Mapping"};
   const char* cfgName[]={"Replace/Replace","Replace/Multiply","Normal/Multiply"};
   std::cout<<shapeName[shape]<<" "<<cfgName[cfg]
     <<" serial_ms "<<std::fixed<<std::setprecision(3)<<median[0]
     <<" adaptive_ms "<<median[1]<<" gain "<<median[0]/median[1]<<"x\n";
  }
 }
 std::cout<<"checksum "<<checksum<<"\n";
}
