#include "raster_core.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile uint64_t sink=0;
int main(){
 Settings s{};
 Gradient g(s,{{0,{.3f,.1f,.9f,.6f}},
               {.4f,{.8f,.5f,.1f,.8f}},
               {1,{.1f,.8f,.2f,1.f}}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[(size_t)i]=g.sample(i/4096.f);
 auto packed8=makePacked8(ramp);
 auto packed16=makePacked16(ramp);
 for(int width:{1920,3840}){
  const int height=width==1920?1080:2160;
  for(int depth=0;depth<3;depth++){
   const int bpp=depth==0?4:depth==1?8:16;
   std::vector<uint8_t> buf((size_t)width*height*bpp);
   WritableBuffer out{buf.data(),width,height,width*bpp,0,0};
   for(int sh=0;sh<2;sh++){
    s=Settings{};
    s.shape=sh==0?Shape::Linear:Shape::Radial;
    if(sh==0){
      s.startX=width*.5f;s.startY=0;
      s.endX=s.startX;s.endY=64;
    }else{
      s.startX=width*.5f;s.startY=height*.5f;
      s.endX=s.startX+64;s.endY=s.startY;
    }
    const blend::Config cfg{blend::Mode::Replace,blend::Mode::Replace,0};
    Buffer empty{};
    render(empty,out,(Depth)depth,s,ramp,&packed8,&packed16,cfg);
    std::vector<double> samples;
    for(int iter=0;iter<6;iter++){
     auto begin=std::chrono::steady_clock::now();
     render(empty,out,(Depth)depth,s,ramp,&packed8,&packed16,cfg);
     auto end=std::chrono::steady_clock::now();
     samples.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
    }
    std::sort(samples.begin(),samples.end());
    sink+=buf[0]+buf[buf.size()/2]+buf.back();
    std::cout<<width<<"x"<<height<<" "<<(depth==0?8:depth==1?16:32)
             <<" "<<(sh==0?"Linear_vertical_short":"Radial_small")
             <<" median_ms "<<std::fixed<<std::setprecision(4)<<samples[3]
             <<" min_ms "<<samples[0]<<"\n";
   }
  }
 }
 std::cout<<"sink "<<sink<<"\n";
}
