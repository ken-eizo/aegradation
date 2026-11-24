#include "raster_core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
bool renderScalarReference(const Buffer&,const WritableBuffer&,Depth,
 const Settings&,const Ramp&,const PackedRamp8*,const PackedRamp16*);
bool scalarBlendReference(const Buffer&,const WritableBuffer&,Depth,
 const Settings&,const Ramp&,const PackedRamp8*,const PackedRamp16*,const blend::Config&);
static bool compare(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,
                  int w,int h,int stride,int depth){
 const int bpp=depth==0?4:depth==1?8:16;
 const double maxerr=depth==0?2:depth==1?5:0.001;
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  const uint8_t* l=a.data()+(size_t)y*stride+x*bpp;
  const uint8_t* r=b.data()+(size_t)y*stride+x*bpp;
  for(int c=0;c<4;c++){
   double err=0;
   if(depth==0)err=std::abs((int)l[c]-(int)r[c]);
   else if(depth==1){
    uint16_t q1=0,q2=0;std::memcpy(&q1,l+c*2,2);
    std::memcpy(&q2,r+c*2,2);
    err=std::abs((int)q1-(int)q2);
   }else{
    float q1=0,q2=0;std::memcpy(&q1,l+c*4,4);
    std::memcpy(&q2,r+c*4,4);
    err=std::abs((double)q1-(double)q2);
   }
   if(!(err<=maxerr)){
    std::cerr<<"Mismatch pixel "<<x<<","<<y<<" channel "<<c
      <<" depth "<<depth<<" difference "<<err<<"\n";return false;
   }
  }
 }
 return true;
}
int main(){
 Settings base{};
 Gradient gradient(base,{{0.f,{.2f,.5f,.9f,.8f}},
                         {.24f,{.9f,.2f,.6f,.42f}},
                         {1.f,{.1f,.8f,.2f,1.f}}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[i]=gradient.sample(i/4096.f);
 auto ramp8=makePacked8(ramp);
 auto ramp16=makePacked16(ramp);
 constexpr int w=96,h=72;
 int cases=0;
 for(int depth=0;depth<3;depth++){
  const int bpp=depth==0?4:depth==1?8:16;
  const int stride=w*bpp+32;
  std::vector<uint8_t> source((size_t)stride*h),avx(source.size()),ref(source.size());
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   const float alpha=((x/17+y/7)%5==0)?.5f:1.f;
   const float b=.15f+(.5f*(x%17)/17.f);
   auto* p=source.data()+(size_t)y*stride+(size_t)x*bpp;
   if(depth==0){Pixel8 v{u8(alpha),u8(alpha*b),u8(alpha*.2f),u8(alpha*.5f)};
     std::memcpy(p,&v,4);}
   if(depth==1){Pixel16 v{(uint16_t)(alpha*32768),
     (uint16_t)(alpha*b*32768),(uint16_t)(alpha*.2f*32768),
     (uint16_t)(alpha*.5f*32768)};std::memcpy(p,&v,8);}
   if(depth==2){Pixel32 v{alpha,alpha*b,alpha*.2f,alpha*.5f};
     std::memcpy(p,&v,16);}
  }
  WritableBuffer outA{avx.data(),w,h,stride,0,0};
  WritableBuffer outB{ref.data(),w,h,stride,0,0};
  // A source-independent Radial gradient with exact mirror centers.
  for(float cy:{15.f,15.5f,35.f,35.5f}){
   Settings s{};
   s.shape=Shape::Radial;s.startX=23;s.startY=cy;
   s.endX=46;s.endY=cy;
   const blend::Config cfg{blend::Mode::Replace,blend::Mode::Replace,0};
   Buffer empty{};
   if(!render(empty,outA,(Depth)depth,s,ramp,&ramp8,&ramp16,cfg)||
      !scalarBlendReference(empty,outB,(Depth)depth,s,ramp,&ramp8,&ramp16,cfg)||
      !compare(avx,ref,w,h,stride,depth))return 10+depth;
   ++cases;
  }
  // Mapping input with repeated and non-repeated scanlines, e.g. a solid
  // layer with an effect applied. Source content changes between frames.
  Settings map{};
  map.shape=Shape::Mapping;
  Buffer in{source.data(),w,h,stride,0,0};
  for(int frame=0;frame<3;frame++){
   for(int y=0;y<h;y++){
    const int original=(y/9)*9;
    if(y!=original)
     std::memcpy(source.data()+(size_t)y*stride,
                 source.data()+(size_t)original*stride,
                 (size_t)w*bpp);
   }
   if(frame==1){source[0]=0;source[(size_t)9*stride]=127;}
   if(frame==2){source[0]=255;source[(size_t)27*stride+2]=18;}
   if(!render(in,outA,(Depth)depth,map,ramp,&ramp8,&ramp16)||
      !renderScalarReference(in,outB,(Depth)depth,map,ramp,&ramp8,&ramp16)||
      !compare(avx,ref,w,h,stride,depth))return 20+depth;
   ++cases;
  }
 }
 std::cout<<"PASS spatial row symmetry/reuse "<<cases<<" frames, "
          <<"RGB+alpha 8/16/32bpc, scalar parity, changed Mapping source\n";
}
