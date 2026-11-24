#include "raster_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
bool scalarBlendReference(const Buffer&,const WritableBuffer&,Depth,
 const Settings&,const Ramp&,const PackedRamp8*,const PackedRamp16*,const blend::Config&);
int main(){
 Settings base{};
 Gradient grad(base,{{0,{.3f,.7f,.2f,.2f}},
                     {.4f,{.9f,.2f,.6f,.7f}},
                     {1,{.15f,.65f,.95f,.95f}}});
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=grad.sample(i/4096.f);
 auto packed8=makePacked8(ramp);
 auto packed16=makePacked16(ramp);
 int tests=0;
 constexpr int W=183,H=117;
 const blend::Config cfg{blend::Mode::Replace,blend::Mode::Replace,0};
 for(int depth=0;depth<3;depth++)for(int shape=0;shape<2;shape++)
 for(int variant=0;variant<10;variant++){
  const int bpp=depth==0?4:depth==1?8:16;
  const int stride=(W+5)*bpp;
  std::vector<uint8_t> avx((size_t)stride*H,0xcc),scalar((size_t)stride*H,0xcc);
  Settings s{};
  s.shape=shape==0?Shape::Linear:Shape::Radial;
  s.startX=variant%3==0?-1000.f:variant%3==1?91.f:60.f;
  s.startY=variant%3==0?-900.f:variant%3==1?47.f:8.f;
  s.endX=s.startX+(variant%2?2.f:180.f);
  s.endY=s.startY+(variant%2?0.f:130.f);
  if(variant==2||variant==4)s.invert=true;
  if(variant==5)s.loop=true;
  if(variant==6)s.reflect=true;
  if(variant==7)s.offsetPx=-30.f;
  if(variant==8)s.offsetPx=40.f;
  if(variant==9)s.scalePercent=61.f;
  WritableBuffer A{avx.data(),W,H,stride,5,4},B{scalar.data(),W,H,stride,5,4};
  Buffer none{};
  if(!render(none,A,(Depth)depth,s,ramp,&packed8,&packed16,cfg)||
     !scalarBlendReference(none,B,(Depth)depth,s,ramp,&packed8,&packed16,cfg))
    return 1;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++)for(int c=0;c<4;c++){
   const size_t i=(size_t)y*stride+(size_t)x*bpp+c*(bpp/4);
   double error=0;
   if(depth==0)error=std::abs((int)avx[i]-(int)scalar[i]);
   else if(depth==1){
    uint16_t l,r;std::memcpy(&l,avx.data()+i,2);
    std::memcpy(&r,scalar.data()+i,2);
    error=std::abs((int)l-(int)r);
   }else{
    float l,r;std::memcpy(&l,avx.data()+i,4);
    std::memcpy(&r,scalar.data()+i,4);
    error=std::abs((double)l-r);
   }
   if(error>(depth==0?2:depth==1?5:0.001)){
    std::cerr<<"FAIL flat row depth "<<depth<<" shape "<<shape
      <<" variant "<<variant<<" pixel "<<x<<","<<y<<" c "<<c<<" error "<<error<<"\n";
    return 2;
   }
  }
  ++tests;
 }
 std::cout<<"PASS: "<<tests<<" flat/end/start/loop/reflect/invert cases across "
          <<"8/16/32bpc, Linear/Radial, scalar reference parity\n";
}
