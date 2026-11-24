#include "raster_core.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile uint64_t sink=0;
int main(){
 Settings settings;
 settings.startX=128;settings.startY=512;
 settings.endX=1728;settings.endY=512;
 settings.shape=Shape::Linear;
 Gradient grad(settings,{{0,{1.f,0.f,0.f,1.f}},
                       {.5f,{0.f,1.f,0.f,.5f}},
                       {1,{0.f,0.f,1.f,1.f}}});
 Ramp lut{};
 for(int i=0;i<=4096;i++)lut[i]=grad.sample(i/4096.f);
 auto packed8=makePacked8(lut);
 auto packed16=makePacked16(lut);
 Buffer noInput{};
 for(auto width:{1920,3840}){
  int height=width==1920?1080:2160;
  for(int dep=0;dep<3;dep++){
   const int bpp=dep==0?4:dep==1?8:16;
   std::vector<uint8_t> dst((size_t)width*height*bpp);
   WritableBuffer out{dst.data(),width,height,width*bpp,0,0};
   for(auto shape:{Shape::Linear,Shape::Radial,Shape::Angle}){
    settings.shape=shape;
    if(shape==Shape::Linear)settings.endY=settings.startY;
    blend::Config mode{blend::Mode::Replace,blend::Mode::Replace,0};
    if(!render(noInput,out,(Depth)dep,settings,lut,&packed8,&packed16,mode))return 1;
    std::vector<double> times;
    for(int j=0;j<4;j++){
     settings.startX+=(float).25;
     if(shape!=Shape::Linear)settings.endY+=(float).20;
     auto start=std::chrono::steady_clock::now();
     render(noInput,out,(Depth)dep,settings,lut,&packed8,&packed16,mode);
     auto end=std::chrono::steady_clock::now();
     times.push_back(std::chrono::duration<double,std::milli>(end-start).count());
    }
    std::sort(times.begin(),times.end());
    sink+=dst[0]+dst[dst.size()/2]+dst.back();
    std::cout<<width<<"x"<<height<<" "<<(dep==0?"8":dep==1?"16":"32")
      <<" "<<(shape==Shape::Linear?"Linear":shape==Shape::Radial?"Radial":"Angle")
      <<" source-independent optimized ms="<<times[1]<<"; min="<<times[0]<<"\n";
   }
  }
 }
 std::cout<<"checksum "<<sink<<"\n";
}
