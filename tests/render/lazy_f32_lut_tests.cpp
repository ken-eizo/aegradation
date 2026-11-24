#include "../../src/render/cpu/lut_cache.hpp"
#include "../../src/render/cpu/raster_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 const blend::Config configs[]={
  {blend::Mode::Replace,blend::Mode::Replace,0},
  {blend::Mode::Replace,blend::Mode::Multiply,0},
  {blend::Mode::Normal,blend::Mode::Multiply,0},
  {blend::Mode::Normal,blend::Mode::Normal,0},
  {blend::Mode::Screen,blend::Mode::Add,0}
 };
 constexpr int W=161,H=73,STRIDE=(W+7)*16;
 std::vector<uint8_t> source((size_t)STRIDE*H);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const float alpha=((x+3*y)%11==0)?.3f:1.f;
  Pixel32 p{alpha,alpha*.23f,alpha*.77f,alpha*.56f};
  std::memcpy(source.data()+(size_t)y*STRIDE+x*16,&p,16);
 }
 int cases=0;
 for(int count:{2,3}){
  const Stop stops[3]={
   {0,{.12f,.83f,.32f,.13f}},
   {.43f,{.84f,.13f,.93f,.67f}},
   {1,{.93f,.62f,.15f,.99f}}
  };
  const Stop actual[3]={stops[0],count==3?stops[1]:stops[2],stops[2]};
  std::vector<Stop> v(actual,actual+count);
  Gradient gradient({},v);
  Ramp ready{};Ramp missing{};
  for(int i=0;i<=4096;i++)ready[(size_t)i]=gradient.sample(i/4096.f);
  DirectRamp direct{stops[0].color,stops[2].color,
                    stops[1].color,stops[1].position,count};
  gradation::lutcache::Pool cache;
  auto key=gradation::lutcache::makeKey(Space::SRGB,v.data(),(uint32_t)count);
  auto entry=cache.get(key,false);
  if(!entry||entry->rampBuilds.load()!=0){
   std::cerr<<"Lazy cache incorrectly prepared in PreRender\n";return 2;
  }
  for(int shape=0;shape<4;shape++)for(int mode=0;mode<5;mode++){
   for(int transform=0;transform<4;transform++){
    Settings s{};s.shape=(Shape)shape;s.startX=39;s.startY=15;
    s.endX=138;s.endY=51;
    if(transform==1)s.invert=true;
    if(transform==2)s.loop=true;
    if(transform==3){s.offsetPx=21;s.reflect=true;}
    std::vector<uint8_t> expected((size_t)STRIDE*H,0x33);
    std::vector<uint8_t> actual((size_t)STRIDE*H,0x77);
    WritableBuffer a{expected.data(),W,H,STRIDE,0,0};
    WritableBuffer b{actual.data(),W,H,STRIDE,0,0};
    Buffer input{source.data(),W-9,H-5,STRIDE,4,2};
    if(mode==0&&shape!=3)input={};
    if(!render(input,a,Depth::F32,s,ready,nullptr,nullptr,configs[mode],&direct)||
       !render(input,b,Depth::F32,s,missing,nullptr,nullptr,configs[mode],&direct))
       return 3;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){
     const uint8_t* pa=expected.data()+(size_t)y*STRIDE+x*16;
     const uint8_t* pb=actual.data()+(size_t)y*STRIDE+x*16;
     if(std::memcmp(pa,pb,16)!=0){
      std::cerr<<"F32 renderer used unprepared LUT count "<<count
       <<" shape "<<shape<<" mode "<<mode<<" transform "<<transform
       <<" xy "<<x<<","<<y<<"\n";return 4;
     }
    }
    cases++;
   }
  }
  if(entry->rampBuilds.load()!=0){
   std::cerr<<"Rendering with direct path mutated lazy ramp\n";return 5;
  }
  // A later non-direct renderer can request the same key: prepare exactly
  // once without corrupting the cache entry shared with prior MFR threads.
  const auto eager=cache.get(key,true);
  if(eager.get()!=entry.get()||entry->rampBuilds.load()!=1){
   std::cerr<<"Lazy-to-eager cache transition did not build once\n";return 6;
  }
 }
 std::cout<<"PASS "<<cases<<" unprepared-LUT 32bpc direct 2/3-stop cases "
   <<"across all shapes, blends, odd widths, cropped inputs, transforms; "
   <<"deferred cache entry builds exactly once upon demand\n";
}
