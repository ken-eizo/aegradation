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
 const Settings original{};
 Gradient grad(original,{{0.f,direct.first},{1.f,direct.last}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[(size_t)i]=grad.sample(i/4096.f);
 constexpr int W=157,H=91;
 constexpr int STRIDE=(W+7)*16;
 std::vector<uint8_t> src((size_t)STRIDE*H);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const float a=(float)((x*17+y*7)%101)/100.f;
  const float red=(float)((x+y)%63)/62.f*a;
  const float green=(float)((x*31+5*y)%97)/96.f*a;
  const float blue=((x+y)%23==0?1.7f:0.7f)*a;
  const Pixel32 p{a,red,green,blue};
  std::memcpy(src.data()+(size_t)y*STRIDE+(size_t)x*16,&p,16);
 }
 int comparisons=0;double maxErr=0;
 for(int sh=0;sh<4;sh++){
  for(int variant=0;variant<5;variant++){
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
   for(int bm=0;bm<6;bm++){
    const blend::Config cfgs[]={
     {blend::Mode::Replace,blend::Mode::Replace,0},
     {blend::Mode::Replace,blend::Mode::Multiply,0},
     {blend::Mode::Normal,blend::Mode::Multiply,0},
     {blend::Mode::Normal,blend::Mode::Normal,0},
     {blend::Mode::Screen,blend::Mode::Add,0},
     {blend::Mode::Color,blend::Mode::Normal,0}};
    const blend::Config cfg=cfgs[bm];
    for(int crop=0;crop<2;crop++){
     const int DW=crop?171:W,DH=crop?103:H,OUTSTRIDE=(DW+9)*16;
     std::vector<uint8_t> a((size_t)OUTSTRIDE*DH,0xda),
       b((size_t)OUTSTRIDE*DH,0xdb);
     Buffer in{src.data(),W,H,STRIDE,crop?14:0,crop?9:0};
     WritableBuffer da{a.data(),DW,DH,OUTSTRIDE,crop?5:0,crop?4:0};
     WritableBuffer db{b.data(),DW,DH,OUTSTRIDE,crop?5:0,crop?4:0};
     if(!render(in,da,Depth::F32,s,ramp,nullptr,nullptr,cfg,nullptr)||
        !render(in,db,Depth::F32,s,ramp,nullptr,nullptr,cfg,&direct)){
      std::cerr<<"Render failed unexpectedly\n";return 2;
     }
     for(int y=0;y<DH;y++)for(int x=0;x<DW;x++){
      for(int channel=0;channel<4;channel++){
       float lhs,rhs;
       std::memcpy(&lhs,a.data()+(size_t)y*OUTSTRIDE+x*16+4*channel,4);
       std::memcpy(&rhs,b.data()+(size_t)y*OUTSTRIDE+x*16+4*channel,4);
       const double err=std::abs((double)lhs-rhs);
       if(err>maxErr)maxErr=err;
       if(!std::isfinite(lhs)||!std::isfinite(rhs)||err>0.0002){
        std::cerr<<"mismatch shape "<<sh<<" var "<<variant
          <<" color/alpha "<<bm<<" cropped "<<crop
          <<" pixel "<<x<<","<<y<<" channel "<<channel
          <<" got "<<rhs<<" ref "<<lhs<<" err "<<err<<"\n";
        return 3;
       }
      }
     }
     comparisons++;
    }
   }
  }
 }
 std::cout<<"PASS: "<<comparisons<<" cases, all 4 shapes, source+cropped ROI, "
   <<"6 blend modes, 32bpc; max float error "<<maxErr<<"\n";
}
