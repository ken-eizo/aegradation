#include "raster_core.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile unsigned long long sinkTiles=0;
int main(){
 Settings s{};
 s.startX=420;s.startY=315;s.endX=1680;s.endY=890;
 s.scalePercent=100;
 Gradient g(s,{{0,{.1f,.7f,.3f,.16f}},
               {.5f,{.9f,.2f,.1f,.82f}},
               {1,{.4f,.7f,.9f,1.f}}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 const PackedRamp8 p8=makePacked8(ramp);
 const PackedRamp16 p16=makePacked16(ramp);
 constexpr int W=256,H=128;
 for(int depth=0;depth<3;depth++){
  const int bpp=depth==0?4:depth==1?8:16;
  std::vector<uint8_t> pixels((size_t)W*H*bpp,0),out(pixels.size());
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const float a=((x+7*y)%19==0)?0.15f:1.f;
   auto* p=pixels.data()+((size_t)y*W+x)*bpp;
   if(depth==0){Pixel8 v{u8(a),u8(a*.5f),u8(a*.3f),u8(a*.7f)};
     std::memcpy(p,&v,4);}
   if(depth==1){Pixel16 v{(uint16_t)(a*32768.f),
     (uint16_t)(a*.5f*32768.f),(uint16_t)(a*.3f*32768.f),
     (uint16_t)(a*.7f*32768.f)};std::memcpy(p,&v,8);}
   if(depth==2){Pixel32 v{a,a*.5f,a*.3f,a*.7f};
     std::memcpy(p,&v,16);}
  }
  Buffer in{pixels.data(),W,H,W*bpp,640,410};
  WritableBuffer dst{out.data(),W,H,W*bpp,640,410};
  for(auto shape:{Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping}){
   s.shape=shape;
   for(int mode=0;mode<2;mode++){
    if(mode==1&&shape==Shape::Mapping)continue;
    const blend::Config cfg=mode==0?
       blend::Config{blend::Mode::Replace,blend::Mode::Multiply,0}:
       blend::Config{blend::Mode::Replace,blend::Mode::Replace,0};
    Buffer input=mode==1?Buffer{}:in;
    render(input,dst,(Depth)depth,s,ramp,&p8,&p16,cfg);
    std::vector<double> timings;
    for(int i=0;i<22;i++){
     auto t0=std::chrono::steady_clock::now();
     if(!render(input,dst,(Depth)depth,s,ramp,&p8,&p16,cfg))return 1;
     auto t1=std::chrono::steady_clock::now();
     timings.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());
    }
    std::sort(timings.begin(),timings.end());
    sinkTiles+=out[0]+out[out.size()/2]+out.back();
    const char* names[]={"Linear","Radial","Angle","Mapping"};
    std::cout<<(depth==0?8:depth==1?16:32)<<"bpc "<<names[(int)shape]
      <<" "<<(mode==0?"source alpha Multiply":"generator Replace")
      <<" tile 256x128 median_ms "<<std::fixed<<std::setprecision(4)
      <<timings[timings.size()/2]<<"\n";
   }
  }
 }
 std::cout<<"checksum "<<sinkTiles<<"\n";
}
