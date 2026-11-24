#include "raster_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
bool scalarBlendReference(const Buffer&,const WritableBuffer&,Depth,const Settings&,
 const Ramp&,const PackedRamp8*,const PackedRamp16*,const blend::Config&);
static int errors=0;
int main(){
 Settings settings;
 Gradient grad(settings,{{0,{.1f,.7f,.3f,.15f}},{.5f,{.9f,.1f,.8f,.55f}},
                          {1,{.4f,.7f,.8f,.9f}}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[i]=grad.sample(i/4096.f);
 auto packed8=makePacked8(ramp);
 auto packed16=makePacked16(ramp);
 const int chosenColor[]={0,1,3,5,17,25,28,29,31,32};
 const int chosenAlpha[]={0,1,2,5,10,12,18,26,28};
 for(int depth=0;depth<3;depth++){
  int bpp=depth==0?4:depth==1?8:16;
  const int srcw=21,srch=7,srcpitch=srcw*bpp+20;
  std::vector<uint8_t> src((size_t)srcpitch*srch);
  for(int y=0;y<srch;y++)for(int x=0;x<srcw;x++){
   float a=(x+y)%7/6.f;
   auto* ptr=src.data()+(size_t)y*srcpitch+(size_t)x*bpp;
   if(depth==0){
    Pixel8 p{u8(a),u8(a*.35f),u8(a*.5f),u8(a*.9f)};
    std::memcpy(ptr,&p,4);
   }else if(depth==1){
    Pixel16 p{(uint16_t)std::round(a*32768.f),
              (uint16_t)std::round(a*.35f*32768.f),
              (uint16_t)std::round(a*.5f*32768.f),
              (uint16_t)std::round(a*.9f*32768.f)};
    std::memcpy(ptr,&p,8);
   }else{
    Pixel32 p{a,a*.35f,a*.5f,a*.9f};
    std::memcpy(ptr,&p,16);
   }
  }
  for(int scenario=0;scenario<4;scenario++){
   constexpr int dw=79,dh=19;
   const int row=dw*bpp+28;
   std::vector<uint8_t> dst0((size_t)row*dh,0x11),dst1(dst0);
   Buffer in{src.data(),srcw,srch,srcpitch,
      scenario==0?18:scenario==1?18:scenario==2?-35:0,
      scenario==0?5:scenario==1?5:scenario==2?-17:0};
   WritableBuffer out0{dst0.data(),dw,dh,row,scenario==1?19:0,scenario==1?6:0};
   WritableBuffer out1{dst1.data(),dw,dh,row,out0.originX,out0.originY};
   if(scenario==3)in={nullptr,0,0,0,0,0};
   for(auto shape:{Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping}){
    settings.shape=shape;
    settings.startX=20;settings.startY=7;
    settings.endX=54;
    settings.endY=(shape==Shape::Linear&&scenario==3)?settings.startY:19;
    for(int c:chosenColor)for(int a:chosenAlpha){
     const blend::Config cfg{(blend::Mode)c,(blend::Mode)a,13};
     if(!render(in,out0,(Depth)depth,settings,ramp,&packed8,&packed16,cfg)
       ||!scalarBlendReference(in,out1,(Depth)depth,settings,ramp,&packed8,&packed16,cfg)){
      std::cerr<<"render rejected source ROI\n";return 1;
     }
     for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){
      const uint8_t* s=dst0.data()+(size_t)y*row+(size_t)x*bpp;
      const uint8_t* t=dst1.data()+(size_t)y*row+(size_t)x*bpp;
      for(int k=0;k<4;k++){
       double delta=0;
       if(depth==0)delta=std::abs((int)s[k]-(int)t[k]);
       else if(depth==1){
        uint16_t v0=0,v1=0;std::memcpy(&v0,s+2*k,2);std::memcpy(&v1,t+2*k,2);
        delta=std::abs((int)v0-(int)v1);
       }else{
        float v0=0,v1=0;std::memcpy(&v0,s+4*k,4);std::memcpy(&v1,t+4*k,4);
        delta=std::abs((double)v0-(double)v1);
       }
       const double maxDelta=depth==0?2:depth==1?8:0.002;
       if(!std::isfinite(delta)||delta>maxDelta){
        std::cerr<<"ROI mismatch: depth="<<depth<<" scenario="<<scenario
        <<" shape="<<(int)shape<<" color="<<c<<" alpha="<<a
        <<" x="<<x<<" y="<<y<<" ch="<<k<<" delta="<<delta<<"\n";
        return 2;
       }
      }
     }
     errors++;
    }
   }
  }
 }
 std::cout<<"SIMD cropped/empty source: "<<errors
          <<" cases, 8/16/32bpc, all 4 shapes, source/target ROI, PASS\n";
}
