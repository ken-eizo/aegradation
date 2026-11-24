#include "../../src/render/cpu/raster_core.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int W=19,H=13;
 const std::array<Space,14> spaces={Space::SRGB,Space::LinearSRGB,
   Space::HSV,Space::HSL,Space::HSI,Space::Oklab,Space::Oklch,
   Space::CMYK,Space::YCbCr,Space::YCgCo,Space::CIECAM02,
   Space::HCT,Space::LMS,Space::Spectral};
 int cases=0;double worst=0;
 for(const Space space:spaces){
  Settings settings{};settings.space=space;
  Gradient gradient(settings,{
    {0.f,{.12f,.69f,.22f,0.f}},
    {.41f,{.94f,.23f,.86f,0.f}},
    {1.f,{.61f,.86f,.19f,0.f}}
  });
  Ramp ramp{};
  for(int i=0;i<=4096;i++)ramp[(size_t)i]=gradient.sample(i/4096.f);
  const auto p8=makePacked8(ramp);
  const auto p16=makePacked16(ramp);
  for(int depth=0;depth<3;depth++){
   const int bytes=depth==0?4:depth==1?8:16;
   const int stride=(W+5)*bytes;
   std::vector<uint8_t> src((size_t)stride*H),dst(src.size(),0xa5);
   for(int y=0;y<H;y++)for(int x=0;x<W;x++){
    const float a=(x+3*y)%13==0?0.f:(x+y)%7==0?.6f:1.f;
    const size_t i=(size_t)y*stride+(size_t)x*bytes;
    if(depth==0){const Pixel8 p{u8(a),u8(a*.45f),u8(a*.88f),u8(a*.26f)};
     std::memcpy(src.data()+i,&p,4);}
    else if(depth==1){
     const Pixel16 p{(uint16_t)(a*32768),
      (uint16_t)(a*.45f*32768),(uint16_t)(a*.88f*32768),
      (uint16_t)(a*.26f*32768)};
     std::memcpy(src.data()+i,&p,8);
    }else{const Pixel32 p{a,a*.45f,-a*.88f,a*1.46f};
     std::memcpy(src.data()+i,&p,16);}
   }
   Buffer in{src.data(),W,H,stride,0,0};
   WritableBuffer out{dst.data(),W,H,stride,0,0};
   for(int shape=0;shape<4;shape++)for(int mode=0;mode<blend::ColorChoices;mode++)
   for(blend::Mode alpha:{blend::Mode::Replace,blend::Mode::Multiply}){
    settings.shape=(Shape)shape;settings.startX=3.f;
    settings.startY=8.f;settings.endX=17.f;settings.endY=11.f;
    const blend::Config cfg{(blend::Mode)mode,alpha,0};
    if(!render(in,out,(Depth)depth,settings,ramp,&p8,&p16,cfg))
     return 2;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)for(int c=0;c<4;c++){
     const size_t i=(size_t)y*stride+(size_t)x*bytes+(size_t)c*(bytes/4);
     double v=0;
     if(depth==0)v=dst[i];
     else if(depth==1){
      uint16_t q;std::memcpy(&q,dst.data()+i,2);v=q;
     }else{
      float q;std::memcpy(&q,dst.data()+i,4);v=q;
     }
     worst=std::max(worst,std::abs(v));
     if(v!=0||!std::isfinite(v)){
      std::cerr<<"FAIL zero-alpha equivalence space "<<(int)space
       <<" depth "<<depth<<" shape "<<shape
       <<" Color "<<mode<<" Alpha "<<(int)alpha
       <<" pixel "<<x<<","<<y<<" c "<<c<<" value "<<v<<"\n";
      return 3;
     }
    }
    ++cases;
   }
  }
 }
 std::cout<<"PASS "<<cases<<" combinations: 14 spaces, 8/16/32bpc,"
   <<" 4 shapes, 33 color modes, Alpha Replace/Multiply;"
   <<" canonical zero framebuffer has identical numeric RGBA; max "<<worst<<"\n";
}
