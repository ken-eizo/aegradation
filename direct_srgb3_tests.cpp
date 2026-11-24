#include "raster_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 Settings start{};
 const Stop stops[3]={{0.f,{.12f,.81f,.24f,.13f}},
                      {.43f,{.85f,.13f,.91f,.74f}},
                      {1.f,{.93f,.74f,.15f,.98f}}};
 Gradient grad(start,{stops[0],stops[1],stops[2]});
 DirectRamp direct{stops[0].color,stops[2].color,stops[1].color,.43f,3};
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[(size_t)i]=grad.sample(i/4096.f);
 const auto r8=makePacked8(ramp);
 const auto r16=makePacked16(ramp);
 const blend::Config configs[]={
  {blend::Mode::Replace,blend::Mode::Replace,0},
  {blend::Mode::Replace,blend::Mode::Multiply,0},
  {blend::Mode::Normal,blend::Mode::Multiply,0},
  {blend::Mode::Normal,blend::Mode::Normal,0},
  {blend::Mode::Screen,blend::Mode::Add,0},
  {blend::Mode::Color,blend::Mode::Normal,0}
 };
 constexpr int W=157,H=91;
 int count=0;double worst[3]{};
 for(int depth=0;depth<3;depth++){
  const int bpp=depth==0?4:depth==1?8:16;
  const int stride=(W+7)*bpp;
  std::vector<uint8_t> src((size_t)stride*H);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const float alpha=(float)((x*17+y*7)%101)/100.f;
   const float rr=(float)((x+y)%61)/60.f*alpha;
   const float gg=(float)((x*31+5*y)%97)/96.f*alpha;
   const float bb=((x+y)%23==0?1.5f:.7f)*alpha;
   uint8_t* p=src.data()+(size_t)y*stride+x*bpp;
   if(depth==0){Pixel8 v{u8(alpha),u8(rr),u8(gg),u8(bb)};std::memcpy(p,&v,4);}
   else if(depth==1){Pixel16 v{(uint16_t)std::lround(alpha*32768),
    (uint16_t)std::lround(rr*32768),(uint16_t)std::lround(gg*32768),
    (uint16_t)std::lround(std::clamp(bb,0.f,1.f)*32768)};
    std::memcpy(p,&v,8);}
   else{Pixel32 v{alpha,rr,gg,bb};std::memcpy(p,&v,16);}
  }
  for(int shape=0;shape<4;shape++)for(int variant=0;variant<5;variant++)
   for(int mode=0;mode<6;mode++)for(int crop=0;crop<2;crop++){
   Settings s{};
   s.shape=(Shape)shape;
   s.startX=variant%2?91.25f:38.25f;
   s.startY=variant%2?40.f:15.5f;
   s.endX=variant==3?38.25f:137.5f;
   s.endY=variant==3?15.5f:48.25f;
   if(variant==1)s.loop=true;
   if(variant==2)s.reflect=true;
   if(variant==3)s.invert=true;
   if(variant==4){s.offsetPx=8.4f;s.scalePercent=89.f;}
   const int DW=crop?171:W,DH=crop?103:H;
   const int pitch=(DW+9)*bpp;
   std::vector<uint8_t> a((size_t)pitch*DH,0xdd),b((size_t)pitch*DH,0xbb);
   Buffer in{src.data(),W,H,stride,crop?14:0,crop?9:0};
   WritableBuffer da{a.data(),DW,DH,pitch,crop?5:0,crop?4:0};
   WritableBuffer db{b.data(),DW,DH,pitch,crop?5:0,crop?4:0};
   const DirectRamp* directArg=(depth==0&&mode!=2&&mode!=3)?
                               nullptr:&direct;
   const auto cfg=configs[mode];
   if(!render(in,da,(Depth)depth,s,ramp,&r8,&r16,cfg,nullptr)||
      !render(in,db,(Depth)depth,s,ramp,&r8,&r16,cfg,directArg))
     return 2;
   for(int y=0;y<DH;y++)for(int x=0;x<DW;x++)for(int c=0;c<4;c++){
    const size_t p=(size_t)y*pitch+x*bpp+c*(bpp/4);
    double diff=0;
    if(depth==0)diff=std::abs((int)a[p]-(int)b[p]);
    else if(depth==1){
     uint16_t av,bv;std::memcpy(&av,a.data()+p,2);std::memcpy(&bv,b.data()+p,2);
     diff=std::abs((int)av-(int)bv);
    }else {
     float av,bv;std::memcpy(&av,a.data()+p,4);std::memcpy(&bv,b.data()+p,4);
     diff=std::abs((double)av-bv);
    }
    worst[depth]=std::max(worst[depth],diff);
    if(diff>(depth==0?3:depth==1?16:.0015)||!std::isfinite(diff)){
     std::cerr<<"FAIL depth "<<depth<<" shape "<<shape<<" var "<<variant
      <<" mode "<<mode<<" crop "<<crop<<" xy "<<x<<","<<y
      <<" channel "<<c<<" error "<<diff<<"\n";return 3;
    }
   }
   count++;
  }
 }
 std::cout<<"PASS "<<count<<" 3-stop sRGB segmented AVX2 comparison cases"
  <<"; max channel differences "<<worst[0]<<" 8bpc, "
  <<worst[1]<<" 16bpc, "<<worst[2]<<" F32\n";
}
