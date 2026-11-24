#include "../../src/render/cpu/raster_core.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int W=43,H=12;
 const float inf=std::numeric_limits<float>::infinity();
 const float nan=std::numeric_limits<float>::quiet_NaN();
 std::vector<Pixel32> src((size_t)W*H),out(src.size());
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  Pixel32 p{(x%9==0)?nan:(x%11==0)?inf:(x%13==0)?-inf:(x%5==0)?0.f:(x%3==0)?1.f:.42f,
    (x%7==0)?nan:(x%7==1)?inf:.6f,
    (x%7==2)?-inf:-.7f,
    (x%7==3)?nan:1.6f};
  src[(size_t)y*W+x]=p;
 }
 Buffer input{(const uint8_t*)src.data(),W,H,W*16,0,0};
 WritableBuffer output{(uint8_t*)out.data(),W,H,W*16,0,0};
 Settings s;s.startX=-5;s.startY=2;s.endX=35;s.endY=7;
 Gradient gradient(s,{{0,{.2f,.8f,.3f,.3f}},{1,{.9f,.1f,.7f,.8f}}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[i]=gradient.sample(i/4096.f);
 int count=0;
 for(auto shape:{Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping}){
  s.shape=shape;
  for(int cm=0;cm<blend::ColorChoices;cm++)
  for(int am=0;am<blend::AlphaChoices;am++){
   const blend::Config cfg{(blend::Mode)cm,(blend::Mode)am,12};
   if(!render(input,output,Depth::F32,s,ramp,nullptr,nullptr,cfg)){
    std::cerr<<"render failed\n";return 1;
   }
   for(auto p:out){
    if(!std::isfinite(p.a)||p.a<0.f||p.a>1.f||
       !std::isfinite(p.r)||!std::isfinite(p.g)||!std::isfinite(p.b)){
     std::cerr<<"nonfinite sample cm="<<cm<<" am="<<am<<" shape="<<(int)shape<<"\n";return 2;
    }
   }
   count++;
  }
 }
 // Several concurrent effects with different blend modes, same source.
 std::array<std::vector<Pixel32>,8> result{};
 std::array<std::thread,8> threads{};
 std::atomic<int> failures{0};
 for(int t=0;t<8;t++){
  result[t].resize((size_t)W*H);
  threads[t]=std::thread([&,t](){
   Settings copy=s;copy.shape=(Shape)(t%4);
   const blend::Config cfg{(blend::Mode)((t*5)%blend::ColorChoices),
                           (blend::Mode)((t*3)%blend::AlphaChoices),(uint32_t)t};
   WritableBuffer target{(uint8_t*)result[t].data(),W,H,W*16,0,0};
   for(int rep=0;rep<8;rep++){
    if(!render(input,target,Depth::F32,copy,ramp,nullptr,nullptr,cfg))failures++;
    for(const auto& p:result[t]){
     if(!std::isfinite(p.a)||!std::isfinite(p.r)||!std::isfinite(p.g)||!std::isfinite(p.b))
      failures++;
    }
   }
  });
 }
 for(auto& t:threads)t.join();
 std::cout<<"32bpc nonfinite + mixed blend MFR test: "<<count<<" mode-shape cases, "<<failures.load()<<" errors\n";
 return failures.load()?3:0;
}
