#include "raster_core.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile uint64_t benchmarkSink=0;
int main(){
 Settings settings;settings.shape=Shape::Linear;
 settings.startX=100;settings.startY=200;settings.endX=1600;settings.endY=200;
 Gradient g(settings,{{0,{1,0,0,.25f}},{.5f,{1,1,0,.75f}},{1,{0,.2f,1,1.f}}});
 Ramp ramp{};
 for(int k=0;k<=4096;k++)ramp[k]=g.sample(k/4096.f);
 auto r8=makePacked8(ramp);
 auto r16=makePacked16(ramp);
 for(int width:{1920}){
  const int height=width==1920?1080:2160;
  for(int d=0;d<3;d++){
   int bpp=d==0?4:d==1?8:16;
   std::vector<uint8_t> src((size_t)width*height*bpp),out(src.size());
   for(size_t i=0;i<(size_t)width*height;i++){
    const float alpha=(i%19==0)?0.f:(i%7==0)?.5f:1.f;
    uint8_t* p=src.data()+i*bpp;
    if(d==0){
     Pixel8 v{u8(alpha),u8(.3f*alpha),u8(.5f*alpha),u8(.8f*alpha)};
     std::memcpy(p,&v,bpp);
    }else if(d==1){
     Pixel16 v{(uint16_t)(alpha*32768.f),
               (uint16_t)(.3f*alpha*32768.f),
               (uint16_t)(.5f*alpha*32768.f),
               (uint16_t)(.8f*alpha*32768.f)};
     std::memcpy(p,&v,bpp);
    }else {
     Pixel32 v{alpha,.3f*alpha,.5f*alpha,.8f*alpha};
     std::memcpy(p,&v,bpp);
    }
   }
   Buffer in{src.data(),width,height,width*bpp,0,0};
   WritableBuffer dst{out.data(),width,height,width*bpp,0,0};
   for(auto s: {Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping}){
    settings.shape=s;
    for(auto mode:{blend::Mode::Normal,blend::Mode::Multiply,blend::Mode::Screen,blend::Mode::Color}){
     if(mode==blend::Mode::Color&&width==3840)continue;
     blend::Config cfg{mode,blend::Mode::Multiply,0};
     render(in,dst,(Depth)d,settings,ramp,&r8,&r16,cfg);
     std::vector<double> times;
     for(int i=0;i<3;i++){
      auto t0=std::chrono::steady_clock::now();
      render(in,dst,(Depth)d,settings,ramp,&r8,&r16,cfg);
      auto t1=std::chrono::steady_clock::now();
      times.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());
     }
     std::sort(times.begin(),times.end());
     benchmarkSink+=out[0]+out[out.size()/2]+out.back();
     const char* sn[]={"Linear","Radial","Angle","Mapping"};
     const char* bn=mode==blend::Mode::Normal?"Normal":mode==blend::Mode::Multiply?"Multiply":
                    mode==blend::Mode::Screen?"Screen":"Color";
     std::cout<<width<<"x"<<height<<" "<<(d==0?"8":d==1?"16":"32")
              <<" "<<sn[(int)s]<<" "<<bn<<" ms="<<times[1]<<"\n";
    }
   }
  }
 }
 std::cout<<"checksum="<<benchmarkSink<<"\n";
}
