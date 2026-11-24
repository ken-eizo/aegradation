#include "raster_core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile unsigned long long sink=0;
int main(){
 constexpr int W=1920,H=1080,Bpp=16;
 Settings initial{};
 initial.startX=350;initial.startY=350;
 initial.endX=1550;initial.endY=880;
 const DirectRamp direct{{.16f,.72f,.31f,.45f},
                         {.92f,.19f,.87f,.95f}};
 Gradient g(initial,{{0,direct.first},{1,direct.last}});
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 std::vector<uint8_t> source((size_t)W*H*Bpp),out(source.size()),reference(source.size());
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const float a=((x+y)%7==0)?.4f:1.f;
  const Pixel32 p{a,a*.16f,a*.7f,a*.85f};
  std::memcpy(source.data()+((size_t)y*W+x)*Bpp,&p,16);
 }
 Buffer in{source.data(),W,H,W*Bpp,0,0};
 for(int shape=0;shape<4;shape++){
  Settings settings=initial;settings.shape=(Shape)shape;
  for(int variant=0;variant<2;variant++){
   if(shape==3&&variant==0)continue;
   const blend::Config cfg=variant==0?
    blend::Config{blend::Mode::Replace,blend::Mode::Replace,0}:
    blend::Config{blend::Mode::Normal,blend::Mode::Multiply,0};
   Buffer input=variant==0?Buffer{}:in;
   WritableBuffer expected{reference.data(),W,H,W*Bpp,0,0};
   if(!render(input,expected,Depth::F32,settings,ramp,nullptr,nullptr,cfg,&direct))
    return 2;
   std::cout<<"Shape="<<shape<<" config="<<variant<<"\n";
   for(int n:{1,2,4,6,8,10}){
    auto one=[&](){
     const int stripes=std::min(n,H);
     std::vector<std::thread> workers;
     workers.reserve((size_t)stripes-1);
     auto draw=[&](int id){
      const int y0=id*H/stripes,y1=(id+1)*H/stripes;
      WritableBuffer tile{out.data()+(size_t)y0*W*Bpp,W,y1-y0,
                          W*Bpp,0,y0};
      if(!render(input,tile,Depth::F32,settings,ramp,nullptr,nullptr,cfg,&direct))
       std::abort();
     };
     for(int i=1;i<stripes;i++)workers.emplace_back(draw,i);
     draw(0);
     for(auto& w:workers)w.join();
    };
    one();
    if(std::memcmp(out.data(),reference.data(),out.size())!=0){
     std::cerr<<"Parallel frame differs from serial shape "<<shape
       <<" variant "<<variant<<" threads "<<n<<"\n";return 3;
    }
    std::vector<double> timing;
    for(int k=0;k<7;k++){
     const auto t0=std::chrono::steady_clock::now();
     one();
     const auto t1=std::chrono::steady_clock::now();
     timing.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());
    }
    std::sort(timing.begin(),timing.end());
    sink+=out[(size_t)n*123];
    std::cout<<" workers "<<n<<" median "<<std::fixed
      <<std::setprecision(3)<<timing[3]
      <<" min "<<timing[0]<<" ms\n";
   }
  }
 }
 std::cout<<"sink "<<sink<<"\n";
}
