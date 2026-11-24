#include "../src/render/cpu/raster_core.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile uint64_t checksum=0;
int main(){
 constexpr int W=1024,H=576,bpp=8;
 Settings s{};
 s.startX=130;s.startY=180;s.endX=820;s.endY=480;
 Gradient g({},{{0,{.16f,.7f,.24f,.36f}},
                {.27f,{.89f,.14f,.9f,.77f}},
                {.64f,{.12f,.9f,.45f,.85f}},
                {1,{.96f,.78f,.13f,.99f}}});
 Ramp lut{};for(int i=0;i<=4096;i++)lut[(size_t)i]=g.sample(i/4096.f);
 const PackedRamp16 r16=makePacked16(lut);
 std::vector<uint8_t> source((size_t)W*H*bpp),dest(source.size());
 for(size_t i=0;i<(size_t)W*H;i++){
  const float alpha=(i%17==0)?0.f:(i%11==0)?.4f:1.f;
  const Pixel16 p{(uint16_t)(alpha*32768),
    (uint16_t)(.35f*alpha*32768),
    (uint16_t)(.73f*alpha*32768),
    (uint16_t)(.29f*alpha*32768)};
  std::memcpy(source.data()+i*bpp,&p,bpp);
 }
 Buffer in{source.data(),W,H,W*bpp,0,0};
 WritableBuffer out{dest.data(),W,H,W*bpp,0,0};
 const blend::Config modes[]={
  {blend::Mode::Normal,blend::Mode::Multiply,0},
  {blend::Mode::Multiply,blend::Mode::Normal,0},
  {blend::Mode::Screen,blend::Mode::Add,0},
  {blend::Mode::Color,blend::Mode::Multiply,0}
 };
 for(int sh=0;sh<4;sh++)for(int md=0;md<4;md++){
  s.shape=(Shape)sh;
  auto run=[&](){return render(in,out,Depth::U16,s,lut,nullptr,&r16,modes[md]);};
  if(!run())return 3;
  std::vector<double> samples;
  for(int k=0;k<11;k++){
   const auto start=std::chrono::steady_clock::now();
   if(!run())return 4;
   const auto end=std::chrono::steady_clock::now();
   samples.push_back(std::chrono::duration<double,std::milli>(end-start).count());
  }
  std::sort(samples.begin(),samples.end());
  checksum+=dest[W*H/2]+dest.back();
  const char* names[]={"Linear","Radial","Angle","Mapping"};
  const char* bnames[]={"Normal+Multiply","Multiply+Normal","Screen+Add","Color+Multiply"};
  std::cout<<names[sh]<<" "<<bnames[md]<<" median_ms "
    <<std::fixed<<std::setprecision(4)<<samples[5]<<"\n";
 }
 std::cout<<"checksum "<<checksum<<"\n";
}
