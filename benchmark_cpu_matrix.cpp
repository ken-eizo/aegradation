#include "raster_parallel.hpp"
#include "raster_horizontal32.hpp"
#include "lut_cache.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <numeric>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int w=1920,h=1080;
 const Stop stops[]={{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}};
 auto entry=lutcache::global().get(lutcache::makeKey(Space::SRGB,stops,3));
 auto p8=entry->getPacked8();auto p16=entry->getPacked16();
 auto& ramp=entry->preparedRamp();
 const blend::Config modes[]={
  {blend::Mode::Replace,blend::Mode::Replace,0},
  {blend::Mode::Replace,blend::Mode::Multiply,0},
  {blend::Mode::Normal,blend::Mode::Normal,0},
  {blend::Mode::Screen,blend::Mode::Multiply,0},
  {blend::Mode::SoftLight,blend::Mode::Multiply,0},
  {blend::Mode::Hue,blend::Mode::Normal,0}};
 const char* names[]={"replace","alpha","normal","screen","softlight","hue"};
 std::vector<double> all;uint64_t checksum=0;
 for(auto depth:{Depth::U8,Depth::U16,Depth::F32}){
  const int bpp=depth==Depth::U8?4:depth==Depth::U16?8:16;
  size_t bytes=size_t(w)*h*bpp;
  std::vector<uint8_t> source(bytes),output(bytes),reference(bytes);
  uint32_t seed=3;
  auto rand=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
  for(size_t i=0;i<bytes;i+=bpp){
   const float a=rand(),r=rand()*a,g=rand()*a,b=rand()*a;
   if(depth==Depth::U8){Pixel8 p{u8(a),u8(r),u8(g),u8(b)};std::memcpy(source.data()+i,&p,bpp);}
   else if(depth==Depth::U16){Pixel16 p{uint16_t(a*32768+.5f),uint16_t(r*32768+.5f),uint16_t(g*32768+.5f),uint16_t(b*32768+.5f)};std::memcpy(source.data()+i,&p,bpp);}
   else{Pixel32 p{a,r,g,b};std::memcpy(source.data()+i,&p,bpp);}
  }
  const WritableBuffer dst{output.data(),w,h,w*bpp,0,0};
  const Buffer src{source.data(),w,h,w*bpp,0,0};
  Settings s{};s.startX=310.5f;s.startY=238.7f;s.endX=1574.2f;s.endY=858.4f;
  DirectRamp direct{};
  direct.first=stops[0].color;direct.middle=stops[1].color;
  direct.last=stops[2].color;direct.count=3;direct.middlePosition=stops[1].position;
  const bool useDirect=std::getenv("GRADATION_MATRIX_DIRECT")!=nullptr;
  if(const char* axis=std::getenv("GRADATION_MATRIX_AXIS")){
   if(axis[0]=='h')s.endY=s.startY;
   else if(axis[0]=='v')s.endX=s.startX;
  }
  std::vector<double> depthTimes;
  for(auto shape:{Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping}){
   s.shape=shape;
   if(std::getenv("GRADATION_MATRIX_AXIS")&&shape!=Shape::Linear)continue;
   for(int m=0;m<6;m++){
    const Buffer in=(m==0&&shape!=Shape::Mapping)?Buffer{}:src;
    const auto cfg=modes[m];
    const bool generator=cfg.color==blend::Mode::Replace&&cfg.alpha==blend::Mode::Replace&&shape!=Shape::Mapping;
    const bool allowed=depth==Depth::F32||
      (depth==Depth::U16&&!cfg.legacyFast()&&!generator)||
      (depth==Depth::U8&&cfg.color==blend::Mode::Normal&&
       (cfg.alpha==blend::Mode::Multiply||cfg.alpha==blend::Mode::Normal||cfg.alpha==blend::Mode::Replace));
    const bool poorFit=depth==Depth::F32&&cfg.legacyFast()&&(shape==Shape::Radial||shape==Shape::Mapping);
    const DirectRamp* dr=useDirect&&allowed&&!poorFit?&direct:nullptr;
    auto run=[&]{return parallel::render(in,dst,depth,s,ramp,p8,p16,cfg,dr);};
    if(!run()||!run())return 2;
    std::array<double,7> times{};
    for(auto& t:times){auto start=std::chrono::steady_clock::now();if(!run())return 3;t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
    std::sort(times.begin(),times.end());
    double t=times[3];all.push_back(t);depthTimes.push_back(t);
    checksum+=output[0]+output[bytes/2]+output.back();
    std::cout<<"case,"<<bpp*2<<","<<int(shape)<<","<<names[m]<<","<<std::fixed<<std::setprecision(4)<<t<<"\n";
   }
  }
  std::cout<<"depth "<<bpp*2<<" mean "<<std::accumulate(depthTimes.begin(),depthTimes.end(),0.0)/depthTimes.size()<<"ms\n";
 }
 std::cout<<"workers "<<parallel::configuredWorkers()<<" cases "<<all.size()<<" mean "<<std::accumulate(all.begin(),all.end(),0.0)/all.size()<<" ms checksum "<<checksum<<"\n";
}
