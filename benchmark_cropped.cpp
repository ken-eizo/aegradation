#include "raster_core.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile uint64_t checksum=0;
int main(){
 constexpr int DW=1920,DH=1080,SW=640,SH=360;
 Settings s;s.startX=512;s.startY=300;s.endX=1320;s.endY=700;
 Gradient g(s,{{0,{1.f,.25f,.3f,.1f}},{.4f,{.25f,.8f,.4f,.65f}},
               {1,{.05f,.25f,1.f,.95f}}});
 Ramp ramp{};for(int k=0;k<=4096;k++)ramp[k]=g.sample(k/4096.f);
 auto r8=makePacked8(ramp);auto r16=makePacked16(ramp);
 for(int depth=0;depth<3;depth++){
  const int bpp=depth==0?4:depth==1?8:16;
  const int srcPitch=SW*bpp,dstPitch=DW*bpp;
  std::vector<uint8_t> source((size_t)srcPitch*SH),target((size_t)dstPitch*DH);
  for(int y=0;y<SH;y++)for(int x=0;x<SW;x++){
   const float a=((x+y)%13==0)?.2f:1.f;
   uint8_t* p=source.data()+((size_t)y*SW+x)*bpp;
   if(depth==0){Pixel8 px{u8(a),u8(a*.2f),u8(a*.8f),u8(a*.4f)};std::memcpy(p,&px,4);}
   else if(depth==1){Pixel16 px{(uint16_t)(a*32768),
      (uint16_t)(a*.2f*32768),(uint16_t)(a*.8f*32768),
      (uint16_t)(a*.4f*32768)};std::memcpy(p,&px,8);}
   else {Pixel32 px{a,a*.2f,a*.8f,a*.4f};std::memcpy(p,&px,16);}
  }
  Buffer src{source.data(),SW,SH,srcPitch,0,0};
  WritableBuffer dst{target.data(),DW,DH,dstPitch,0,0};
  for(auto shape:{Shape::Linear,Shape::Angle}){
   s.shape=shape;
   for(auto alpha:{blend::Mode::Multiply,blend::Mode::Replace}){
    blend::Config cfg{blend::Mode::Normal,alpha,0};
    // Warm-up (both LUT and AVX code pages).
    render(src,dst,(Depth)depth,s,ramp,&r8,&r16,cfg);
    std::vector<double> samples;
    for(int i=0;i<3;i++){
     // Start / End moves, but user has NOT changed any gradient stops.
     s.startX+=.5f;s.endX+=.25f;
     const auto begin=std::chrono::steady_clock::now();
     const bool ok=render(src,dst,(Depth)depth,s,ramp,&r8,&r16,cfg);
     const auto end=std::chrono::steady_clock::now();
     if(!ok)return 2;
     samples.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
    }
    std::sort(samples.begin(),samples.end());
    checksum+=(uint64_t)target[0]+target[target.size()/2]+target.back();
    std::cout<<"DEPTH "<<(depth==0?8:depth==1?16:32)
     <<" SHAPE "<<(shape==Shape::Linear?"Linear":"Angle")
     <<" ALPHA "<<(alpha==blend::Mode::Multiply?"Multiply":"Replace")
     <<" median_ms "<<std::fixed<<std::setprecision(3)<<samples[1]
     <<" min "<<samples[0]<<"\n";
   }
  }
 }
 std::cout<<"checksum "<<checksum<<"\n";
}
