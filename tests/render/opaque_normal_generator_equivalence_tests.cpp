#include "../../src/render/cpu/raster_core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 Settings s{};
 Gradient g(s,{{0,{.1f,.3f,.7f,1.f}},
               {.3f,{.95f,.2f,.55f,1.f}},
               {1,{.6f,.9f,.2f,1.f}}});
 Ramp ramp{};for(int k=0;k<=4096;k++)ramp[(size_t)k]=g.sample(k/4096.f);
 const PackedRamp8 p8=makePacked8(ramp);
 const PackedRamp16 p16=makePacked16(ramp);
 for(int depth=0;depth<3;depth++)for(int shape=0;shape<3;shape++){
  const int W=127,H=73,bpp=depth==0?4:depth==1?8:16;
  const int stride=W*bpp+24;
  std::vector<uint8_t> source((size_t)stride*H),first(source.size()),other(source.size());
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const float a=((x+y)%11==0)?0.f:((x*y)%7==0)?0.5f:1.f;
   uint8_t* p=source.data()+(size_t)y*stride+x*bpp;
   if(depth==0){
    Pixel8 v{u8(a),u8(a*.55f),u8(a*.31f),u8(a*.77f)};std::memcpy(p,&v,4);
   }else if(depth==1){
    Pixel16 v{(uint16_t)(a*32768), (uint16_t)(a*.55f*32768),
      (uint16_t)(a*.31f*32768),(uint16_t)(a*.77f*32768)};
    std::memcpy(p,&v,8);
   }else{
    Pixel32 v{a,a*.55f,a*.31f,a*.77f};std::memcpy(p,&v,16);
   }
  }
  Buffer src{source.data(),W,H,stride,0,0};
  WritableBuffer outFirst{first.data(),W,H,stride,0,0};
  WritableBuffer outOther{other.data(),W,H,stride,0,0};
  s.shape=(Shape)shape;s.startX=12;s.endX=89;
  s.startY=19;s.endY=57;
  const blend::Config normal{blend::Mode::Normal,blend::Mode::Normal,0};
  const blend::Config generator{blend::Mode::Replace,blend::Mode::Replace,0};
  if(!render(src,outFirst,(Depth)depth,s,ramp,&p8,&p16,normal)||
     !render({},outOther,(Depth)depth,s,ramp,&p8,&p16,generator))
      return 2;
  double maxError=0;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   for(int c=0;c<4;c++){
    const size_t at=(size_t)y*stride+x*bpp+c*(bpp/4);
    double error=0;
    if(depth==0)error=std::abs((int)first[at]-(int)other[at]);
    else if(depth==1){
      uint16_t a,b;std::memcpy(&a,first.data()+at,2);
      std::memcpy(&b,other.data()+at,2);
      error=std::abs((int)a-(int)b);
    }else{
      float a,b;std::memcpy(&a,first.data()+at,4);
      std::memcpy(&b,other.data()+at,4);
      error=std::abs((double)a-(double)b);
    }
    maxError=std::max(maxError,error);
   }
  }
  const double tolerance=depth==0?2:depth==1?5:.00012;
  if(maxError>tolerance){
    std::cerr<<"Mismatch depth "<<depth<<" shape "<<shape
      <<" max channel error "<<maxError<<"\n";return 3;
  }
  std::cout<<"PASS depth "<<depth<<" shape "<<shape
    <<" max error "<<maxError<<"\n";
 }
}
