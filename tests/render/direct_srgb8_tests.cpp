#include "../../src/render/cpu/raster_core.hpp"
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
 const PackedRamp8 ramp8=makePacked8(ramp);
 constexpr int W=157,H=91,STRIDE=(W+7)*4;
 std::vector<uint8_t> src((size_t)STRIDE*H);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const float a=((x*17+y*7)%101)/100.f;
  const float red=((x+y)%63)/62.f*a;
  const float green=((x*31+5*y)%97)/96.f*a;
  const float blue=((x+y)%23==0?0.9f:0.7f)*a;
  const Pixel8 p{u8(a),u8(red),u8(green),u8(blue)};
  std::memcpy(src.data()+(size_t)y*STRIDE+(size_t)x*4,&p,4);
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
   {blend::Mode::Normal,blend::Mode::Replace,0},
   {blend::Mode::Normal,blend::Mode::Multiply,0},
   {blend::Mode::Normal,blend::Mode::Normal,0},
   {blend::Mode::Replace,blend::Mode::Multiply,0},
   {blend::Mode::HardMix,blend::Mode::Normal,0}};
  for(int bm=0;bm<5;bm++)for(int crop=0;crop<2;crop++){
   const int DW=crop?171:W,DH=crop?103:H,OUTSTRIDE=(DW+9)*4;
   std::vector<uint8_t> a((size_t)OUTSTRIDE*DH,0xda),
                        b((size_t)OUTSTRIDE*DH,0xdb);
   Buffer in{src.data(),W,H,STRIDE,crop?14:0,crop?9:0};
   WritableBuffer da{a.data(),DW,DH,OUTSTRIDE,crop?5:0,crop?4:0};
   WritableBuffer db{b.data(),DW,DH,OUTSTRIDE,crop?5:0,crop?4:0};
   if(!render(in,da,Depth::U8,s,ramp,&ramp8,nullptr,configs[bm],nullptr)||
      !render(in,db,Depth::U8,s,ramp,&ramp8,nullptr,configs[bm],&direct))
      return 2;
   for(int y=0;y<DH;y++)for(int x=0;x<DW;x++){
    for(int c=0;c<4;c++){
     const int lhs=a[(size_t)y*OUTSTRIDE+x*4+c];
     const int rhs=b[(size_t)y*OUTSTRIDE+x*4+c];
     const int err=std::abs(lhs-rhs);
     maxErr=std::max(maxErr,err);
     if(err>2){
      std::cerr<<"mismatch shape "<<sh<<" var "<<variant
        <<" mode "<<bm<<" cropped "<<crop<<" pixel "<<x<<","<<y
        <<" channel "<<c<<" got "<<rhs<<" ref "<<lhs<<" err "<<err<<"\n";
      return 3;
     }
    }
   }
   ++comparisons;
  }
 }
 std::cout<<"PASS "<<comparisons<<" cases, 8bpc direct sRGB Normal & legacy modes, "
          <<"max channel error "<<maxErr<<" of 255\n";
}
