#include "../../src/render/cpu/raster_horizontal32.hpp"
#include <algorithm>
#include <chrono>
#include <atomic>
#include <thread>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 volatile uint32_t checksum=0;
 for(int width:{161,320,1920})for(int height:{43,128,1080}){
  for(int stopsN:{2,3})for(int lutMode=0;lutMode<2;lutMode++)
  for(int trans=0;trans<3;trans++){
   if(width>320&&(stopsN!=2||lutMode!=1||trans!=0))continue;
   Settings s{};
   s.shape=Shape::Linear;
   s.startX=13.25f;s.endX=(float)width*.87f;
   s.startY=17.5f;s.endY=17.5f;
   if(trans==1){s.reflect=true;s.offsetPx=37.f;}
   if(trans==2){s.loop=true;s.invert=true;s.scalePercent=140.f;}
   s.space=lutMode==0?Space::Oklab:Space::SRGB;
   const std::vector<Stop> stops=stopsN==2?
    std::vector<Stop>{{0,{.19f,.72f,.43f,.26f}},{1,{.93f,.14f,.83f,.98f}}}:
    std::vector<Stop>{{0,{.19f,.72f,.43f,.26f}},
      {.43f,{.13f,.91f,.62f,.77f}},{1,{.93f,.14f,.83f,.98f}}};
   Gradient g(s,stops);
   Ramp ramp{};
   for(int i=0;i<=4096;i++)ramp[(size_t)i]=g.sample(i/4096.f);
   const DirectRamp dr=stopsN==2?
     DirectRamp{stops[0].color,stops[1].color}:
     DirectRamp{stops[0].color,stops[2].color,
                stops[1].color,.43f,3};
   const DirectRamp* direct=lutMode==1?&dr:nullptr;
   const size_t count=(size_t)width*height;
   std::vector<Pixel32> input(count),baseline(count),actual(count);
   for(int y=0;y<height;y++)for(int x=0;x<width;x++){
    const size_t at=(size_t)y*width+x;
    float alpha=(x+y)%11==0?0.f:(x*7+y)%13==0?.37f:1.f;
    input[at]={alpha,alpha*.13f,alpha*.81f,alpha*.5f};
   }
   Buffer src{(uint8_t*)input.data(),width,height,width*16,17,9};
   WritableBuffer ref{(uint8_t*)baseline.data(),width,height,width*16,17,9};
   WritableBuffer opt{(uint8_t*)actual.data(),width,height,width*16,17,9};
   const blend::Config cfg{blend::Mode::Replace,blend::Mode::Multiply,0};
   if(!gradation::raster::render(src,ref,Depth::F32,s,ramp,nullptr,nullptr,cfg,direct)||
      !gradation::raster::horizontal32::render(src,opt,s,ramp,direct))
     return 2;
   double maxDelta=0;int differences=0;
   for(size_t j=0;j<count;j++)for(int c=0;c<4;c++){
    const float* b=(const float*)&baseline[j];
    const float* a=(const float*)&actual[j];
    const double delta=std::abs((double)b[c]-a[c]);
    maxDelta=std::max(maxDelta,delta);
    if(std::memcmp(&b[c],&a[c],sizeof(float))!=0)differences++;
   }
   // Same float precision; an epsilon tolerance is allowed only for
   // alternate parenthesization inside Oklab LUT sampling.
   if(maxDelta>1.e-5){
    std::cerr<<"FAIL width "<<width<<" height "<<height
     <<" stops "<<stopsN<<" lutMode "<<lutMode<<" transform "<<trans
     <<" maxDelta "<<maxDelta<<" changed floats "<<differences<<"\n";
    return 3;
   }
   if((width==320||width==1920)&&
      stopsN==2&&lutMode==1&&trans==0){
    auto measure=[&](bool optimized){
     std::vector<double> m;
     for(int k=0;k<11;k++){
      const auto begin=std::chrono::steady_clock::now();
      if(optimized)
       gradation::raster::horizontal32::render(src,opt,s,ramp,direct);
      else
       gradation::raster::render(src,ref,Depth::F32,s,ramp,
                                 nullptr,nullptr,cfg,direct);
      const auto end=std::chrono::steady_clock::now();
      m.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
     }
     std::sort(m.begin(),m.end());
     return m[5];
    };
    const double original=measure(false),changed=measure(true);
    std::cout<<"F32 "<<width<<"x"<<height<<" horizontal 2-stop partial alpha"
      <<" original_ms "<<std::setprecision(4)<<original
      <<" row_cached_ms "<<changed<<" speedup "<<original/changed
      <<" max_err "<<maxDelta<<" differing_floats "<<differences<<"\n";
   }
   checksum+=(uint8_t)(actual[count/2].a*127.f);
  }
 }
 // AE may render ten independently parameterized effect instances on
 // different threads. Every horizontal LUT is immutable and every row
 // cache allocation must be frame-local; compare every frame byte-for-byte
 // with a serial baseline even when the source includes zero-alpha pixels.
 {
  constexpr int TW=320,TH=128,THREADS=10,FRAMES=70;
  std::vector<Pixel32> in((size_t)TW*TH);
  for(int y=0;y<TH;y++)for(int x=0;x<TW;x++){
   const float a=(x+y)%17==0?0.f:(x*3+y)%13==0?.3f:1.f;
   in[(size_t)y*TW+x]={a,a*.3f,a*.7f,a*.4f};
  }
  Buffer src{(uint8_t*)in.data(),TW,TH,TW*16,0,0};
  std::atomic<int> mismatches{0};
  std::array<std::thread,THREADS> threads;
  for(int id=0;id<THREADS;id++)threads[(size_t)id]=std::thread([&,id]{
   Settings s{};s.shape=Shape::Linear;
   s.startX=19.f+id*.4f;s.endX=280.f-id*.7f;
   s.startY=47;s.endY=47;
   const DirectRamp dr{
     {.14f+.01f*id,.7f,.2f,.32f},
     {.78f,.2f,.82f,.93f}
   };
   const std::vector<Stop> stops={
      {0,dr.first},{1,dr.last}};
   Gradient grad(s,stops);
   Ramp ramp{};
   for(int i=0;i<=4096;i++)ramp[(size_t)i]=grad.sample(i/4096.f);
   std::vector<Pixel32> a(in.size()),b(in.size());
   WritableBuffer old{(uint8_t*)a.data(),TW,TH,TW*16,0,0};
   WritableBuffer opt{(uint8_t*)b.data(),TW,TH,TW*16,0,0};
   const blend::Config cfg{blend::Mode::Replace,blend::Mode::Multiply,0};
   for(int frame=0;frame<FRAMES;frame++){
    s.offsetPx=(float)frame*.31f;
    if(!gradation::raster::render(src,old,Depth::F32,s,ramp,
                                  nullptr,nullptr,cfg,&dr)||
       !gradation::raster::horizontal32::render(src,opt,s,ramp,&dr)||
       std::memcmp(a.data(),b.data(),a.size()*sizeof(Pixel32))!=0)
     mismatches.fetch_add(1,std::memory_order_relaxed);
   }
  });
  for(auto& t:threads)t.join();
  if(mismatches.load()!=0){
   std::cerr<<"MFR horizontal32 exact output mismatch "
      <<mismatches.load()<<"\n";return 4;
  }
 }
 std::cout<<"PASS horizontal F32, 2/3 stops, sRGB/Oklab,"
          <<" odd widths, animated transforms, source opacity, "
          <<"direct/LUT, checksum "<<checksum<<"\n";
}
