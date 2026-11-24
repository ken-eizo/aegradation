#include "raster_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 const DirectRamp direct{{.12345f,.9273f,.17421f,.2f},
                         {.98765f,.10043f,.71313f,.95f}};
 Settings original{};
 Gradient grad(original,{{0.f,direct.first},{1.f,direct.last}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[(size_t)i]=grad.sample(i/4096.f);
 const PackedRamp16 ramp16=makePacked16(ramp);
 constexpr int W=157,H=91,STRIDE=(W+7)*8;
 std::vector<uint8_t> src((size_t)STRIDE*H);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const float a=((x*17+y*7)%101)/100.f;
  const float red=((x+y)%63)/62.f*a;
  const float green=((x*31+5*y)%97)/96.f*a;
  const float blue=((x+y)%23==0?0.9f:0.7f)*a;
  const Pixel16 p{(uint16_t)std::lround(a*32768.f),
    (uint16_t)std::lround(red*32768.f),
    (uint16_t)std::lround(green*32768.f),
    (uint16_t)std::lround(blue*32768.f)};
  std::memcpy(src.data()+(size_t)y*STRIDE+(size_t)x*8,&p,8);
 }
 int comparisons=0,maxErr=0;
 for(int sh=0;sh<4;sh++)for(int variant=0;variant<5;variant++){
  Settings s{};
  s.shape=(Shape)sh;
  s.startX=variant%2?91.25f:38.25f;
  s.startY=variant%2?40.f:15.5f;
  s.endX=variant==3?38.25f:137.5f;
  s.endY=variant==3?15.5f:48.25f;
  if(variant==1)s.loop=true;
  if(variant==2)s.reflect=true;
  if(variant==3)s.invert=true;
  if(variant==4){s.offsetPx=8.4f;s.scalePercent=89.f;}
  const blend::Config configs[]={
   {blend::Mode::Replace,blend::Mode::Replace,0},
   {blend::Mode::Replace,blend::Mode::Multiply,0},
   {blend::Mode::Normal,blend::Mode::Multiply,0},
   {blend::Mode::Normal,blend::Mode::Normal,0},
   {blend::Mode::Screen,blend::Mode::Add,0},
   {blend::Mode::Color,blend::Mode::Normal,0}};
  for(int bm=0;bm<6;bm++)for(int crop=0;crop<2;crop++){
   const int DW=crop?171:W,DH=crop?103:H,OUTSTRIDE=(DW+9)*8;
   std::vector<uint8_t> a((size_t)OUTSTRIDE*DH,0xda),
                        b((size_t)OUTSTRIDE*DH,0xdb);
   Buffer in{src.data(),W,H,STRIDE,crop?14:0,crop?9:0};
   WritableBuffer da{a.data(),DW,DH,OUTSTRIDE,crop?5:0,crop?4:0};
   WritableBuffer db{b.data(),DW,DH,OUTSTRIDE,crop?5:0,crop?4:0};
   if(!render(in,da,Depth::U16,s,ramp,nullptr,&ramp16,configs[bm],nullptr)||
      !render(in,db,Depth::U16,s,ramp,nullptr,&ramp16,configs[bm],&direct))
      return 2;
   for(int y=0;y<DH;y++)for(int x=0;x<DW;x++){
    for(int channel=0;channel<4;channel++){
     uint16_t lhs,rhs;
     std::memcpy(&lhs,a.data()+(size_t)y*OUTSTRIDE+x*8+2*channel,2);
     std::memcpy(&rhs,b.data()+(size_t)y*OUTSTRIDE+x*8+2*channel,2);
     const int err=std::abs((int)lhs-(int)rhs);
     maxErr=std::max(maxErr,err);
     if(err>8){
      std::cerr<<"mismatch shape "<<sh<<" var "<<variant
       <<" mode "<<bm<<" cropped "<<crop<<" pixel "<<x<<","<<y
       <<" channel "<<channel<<" got "<<rhs<<" ref "<<lhs<<" err "<<err<<"\n";
      return 3;
     }
    }
   }
   ++comparisons;
  }
 }
 std::cout<<"PASS "<<comparisons<<" cases, 16bpc direct sRGB, max channel error "
          <<maxErr<<" of 32768\n";
}
