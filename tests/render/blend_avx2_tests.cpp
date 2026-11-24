#include "../../src/render/cpu/raster_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
bool scalarBlendReference(const Buffer&,const WritableBuffer&,Depth,const Settings&,
 const Ramp&,const PackedRamp8*,const PackedRamp16*,const blend::Config&);
int main(){
 constexpr int W=26,H=6,PADDING=17;
 Settings settings;settings.startX=3;settings.startY=9;settings.endX=22;
 settings.endY=17;settings.scalePercent=93.0f;settings.offsetPx=2;
 Gradient g({},{{0,{.9f,.15f,.1f,.1f}},{.35f,{.03f,.85f,.4f,.64f}},
                 {1,{.2f,.04f,.98f,1.f}}});
 Ramp lut{};
 for(int k=0;k<=4096;k++)lut[k]=g.sample(k/4096.f);
 auto p8=makePacked8(lut);
 auto p16=makePacked16(lut);
 double maxDiff8=0,maxDiff16=0,maxDiff32=0;
 int combos=0;
 for(int dep=0;dep<3;dep++){
  const int bpp=dep==0?4:dep==1?8:16;
  const int row=W*bpp+PADDING;
  std::vector<uint8_t> src((size_t)row*H,0);
  std::vector<uint8_t> simdOutput(src.size(),0xa5),scalarOutput(src.size(),0xa5);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const float alpha=((x+3*y)%9)/8.f;
   const float red=((x*13+y*3)%47)/46.f;
   const float green=((x*7+y*11)%41)/40.f;
   const float blue=((x*5+y*17)%37)/36.f;
   uint8_t* at=src.data()+(size_t)y*row+(size_t)x*bpp;
   if(dep==0){
    Pixel8 v{u8(alpha),u8(red*alpha),u8(green*alpha),u8(blue*alpha)};
    std::memcpy(at,&v,bpp);
   }else if(dep==1){
    Pixel16 v{(uint16_t)std::lround(alpha*32768.f),
              (uint16_t)std::lround(red*alpha*32768.f),
              (uint16_t)std::lround(green*alpha*32768.f),
              (uint16_t)std::lround(blue*alpha*32768.f)};
    std::memcpy(at,&v,bpp);
   }else{
    Pixel32 v{alpha,red*alpha,green*alpha,blue*alpha};
    std::memcpy(at,&v,bpp);
   }
  }
  Buffer source{src.data(),W,H,row,11,21};
  WritableBuffer simd{simdOutput.data(),W,H,row,11,21};
  WritableBuffer scalar{scalarOutput.data(),W,H,row,11,21};
  for(auto shape:{Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping}){
   settings.shape=shape;
   for(int color=0;color<blend::ColorChoices;color++)
   for(int alpha=0;alpha<blend::AlphaChoices;alpha++){
    const blend::Config cfg{(blend::Mode)color,(blend::Mode)alpha,217};
    if(!render(source,simd,(Depth)dep,settings,lut,&p8,&p16,cfg)||
       !scalarBlendReference(source,scalar,(Depth)dep,settings,lut,&p8,&p16,cfg)){
     std::cerr<<"Render failed\n";return 2;
    }
    combos++;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)for(int ch=0;ch<4;ch++){
     const uint8_t *a=simdOutput.data()+(size_t)y*row+(size_t)x*bpp;
     const uint8_t *b=scalarOutput.data()+(size_t)y*row+(size_t)x*bpp;
     double delta=0;
     if(dep==0)delta=std::abs((int)a[ch]-(int)b[ch]);
     if(dep==1){
      uint16_t av,bv;std::memcpy(&av,a+ch*2,2);std::memcpy(&bv,b+ch*2,2);
      delta=std::abs((int)av-(int)bv);
     }
     if(dep==2){
      float av,bv;std::memcpy(&av,a+ch*4,4);std::memcpy(&bv,b+ch*4,4);
      delta=std::abs((double)av-(double)bv);
     }
     if(dep==0)maxDiff8=std::max(maxDiff8,delta);
     if(dep==1)maxDiff16=std::max(maxDiff16,delta);
     if(dep==2)maxDiff32=std::max(maxDiff32,delta);
     const double tolerance=dep==0?2:dep==1?7:.001;
     if(delta>tolerance||!std::isfinite(delta)){
      std::cerr<<"Mismatch "<<dep<<" "<<(int)shape<<" Color="<<color<<" Alpha="<<alpha
               <<" x="<<x<<" y="<<y<<" c="<<ch<<" diff="<<delta<<"\n";
      return 3;
     }
    }
   }
  }
 }
 std::cout<<"SIMD vs scalar: "<<combos<<" mode/depth/shape configurations, max differences "
 <<maxDiff8<<" (8-bit), "<<maxDiff16<<" (16-bit), "<<maxDiff32<<" (32-bit)\n";
}
