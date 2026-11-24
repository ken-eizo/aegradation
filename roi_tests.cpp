#include "raster_core.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
bool renderScalarReference(const Buffer&,const WritableBuffer&,Depth,
                           const Settings&,const Ramp&,const PackedRamp8*,const PackedRamp16*);
int main(){
 constexpr int IW=61,IH=39,OW=87,OH=51;
 Settings s;
 s.startX=19;s.startY=11;s.endX=53;s.endY=46;
 s.offsetPx=3.7f;s.scalePercent=83.f;s.loop=true;s.reflect=false;
 Gradient gradient({},{{0.f,{.1f,.5f,.3f,1.f}},
                       {.33f,{1.f,.12f,.42f,.62f}},
                       {1.f,{.2f,.1f,.9f,1.f}}});
 Ramp lut{};for(int i=0;i<=4096;i++)lut[i]=gradient.sample(i/4096.f);
 PackedRamp8 p8=makePacked8(lut);
 PackedRamp16 p16=makePacked16(lut);
 int max8=0,max16=0;
 double max32=0;
 const std::array<std::array<int,4>,4> origins={{
  {{0,0,-14,-9}},{{3,4,15,16}},{{-21,-17,-21,-17}},{{140,50,-80,-60}}
 }};
 for(int depth=0;depth<3;depth++)for(int shape=0;shape<4;shape++)
 for(const auto& o:origins){
  const int bpp=depth==0?4:depth==1?8:16;
  const int istride=IW*bpp+13,ostride=OW*bpp+17;
  std::vector<uint8_t> input((size_t)istride*IH,0),a((size_t)ostride*OH,0xcd),
                       b((size_t)ostride*OH,0xcd);
  for(int y=0;y<IH;y++)for(int x=0;x<IW;x++){
   uint8_t* p=input.data()+(size_t)y*istride+(size_t)x*bpp;
   const float alpha=((x*31+y*23)%241)/240.f;
   const float r=((x*7+y)%127)/126.f;
   const float g=((x+y*19)%131)/130.f;
   const float blue=((x*13+y*5)%251)/250.f;
   if(depth==0){
    Pixel8 v{u8(alpha),u8(r*alpha),u8(g*alpha),u8(blue*alpha)};
    std::memcpy(p,&v,4);
   }else if(depth==1){
    Pixel16 v{(uint16_t)std::lround(alpha*32768.f),
              (uint16_t)std::lround(r*alpha*32768.f),
              (uint16_t)std::lround(g*alpha*32768.f),
              (uint16_t)std::lround(blue*alpha*32768.f)};
    std::memcpy(p,&v,8);
   }else{
    Pixel32 v{alpha,r*alpha,g*alpha,blue*alpha};
    std::memcpy(p,&v,16);
   }
  }
  Buffer source{input.data(),IW,IH,istride,o[0],o[1]};
  WritableBuffer dstA{a.data(),OW,OH,ostride,o[2],o[3]};
  WritableBuffer dstB{b.data(),OW,OH,ostride,o[2],o[3]};
  s.shape=(Shape)shape;
  const Depth d=(Depth)depth;
  if(!render(source,dstA,d,s,lut,&p8,&p16) ||
     !renderScalarReference(source,dstB,d,s,lut,&p8,&p16)){
   std::cerr<<"Render failed unexpectedly\n";return 1;
  }
  for(int y=0;y<OH;y++)for(int x=0;x<OW;x++){
   const uint8_t* lhs=a.data()+(size_t)y*ostride+(size_t)x*bpp;
   const uint8_t* rhs=b.data()+(size_t)y*ostride+(size_t)x*bpp;
   for(int channel=0;channel<4;channel++){
    if(depth==0){
     const int err=std::abs((int)lhs[channel]-(int)rhs[channel]);
     max8=std::max(max8,err);
     if(err>2){std::cerr<<"8bit ROI mismatch "<<shape<<" "<<x<<","<<y<<" c"<<channel<<" error="<<err<<"\n";return 2;}
    }else if(depth==1){
     uint16_t va=0,vb=0;
     std::memcpy(&va,lhs+channel*2,2);
     std::memcpy(&vb,rhs+channel*2,2);
     const int err=std::abs((int)va-(int)vb);
     max16=std::max(max16,err);
     if(err>3){std::cerr<<"16bit ROI mismatch "<<shape<<" "<<x<<","<<y<<" c"<<channel<<" error="<<err<<"\n";return 3;}
    }else{
     float va=0,vb=0;
     std::memcpy(&va,lhs+channel*4,4);
     std::memcpy(&vb,rhs+channel*4,4);
     const double err=std::abs((double)va-(double)vb);
     max32=std::max(max32,err);
     if(err>1e-3){std::cerr<<"32bit ROI mismatch "<<shape<<" "<<x<<","<<y<<" c"<<channel<<" error="<<err<<"\n";return 4;}
    }
   }
  }
  for(int y=0;y<OH;y++)for(int pad=OW*bpp;pad<ostride;pad++){
   if(a[(size_t)y*ostride+pad]!=0xcd){
    std::cerr<<"Overwrote padding at row "<<y<<"\n";return 5;
   }
  }
 }
 std::cout<<"ROI clipped SIMD/scalar parity: 3 depths x 4 shapes x 4 origin cases=48, max8="<<max8
          <<" max16="<<max16<<" maxF32="<<max32<<" PASS\n";
}
