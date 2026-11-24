#include "../src/render/cpu/raster_core.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile unsigned long long sinkTiles=0;
int main(){
 Settings s{};
 s.startX=420;s.startY=315;s.endX=1680;s.endY=890;
 const DirectRamp direct{{.1f,.7f,.3f,.16f},{.4f,.7f,.9f,1.f}};
 Gradient g(s,{{0,direct.first},{1,direct.last}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 const PackedRamp8 p8=makePacked8(ramp);
 const PackedRamp16 p16=makePacked16(ramp);
 constexpr int W=256,H=128;
 for(int depth=0;depth<3;depth++){
  const int bpp=depth==0?4:depth==1?8:16;
  std::vector<uint8_t> source((size_t)W*H*bpp),out(source.size());
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const float a=((x+y*7)%19==0)?.15f:1.f;
   uint8_t* p=source.data()+((size_t)y*W+x)*bpp;
   if(depth==0){Pixel8 v{u8(a),u8(a*.5f),u8(a*.3f),u8(a*.7f)};
    std::memcpy(p,&v,4);}
   if(depth==1){Pixel16 v{(uint16_t)(a*32768),
    (uint16_t)(a*.5f*32768),(uint16_t)(a*.3f*32768),
    (uint16_t)(a*.7f*32768)};std::memcpy(p,&v,8);}
   if(depth==2){Pixel32 v{a,a*.5f,a*.3f,a*.7f};std::memcpy(p,&v,16);}
  }
  Buffer input{source.data(),W,H,W*bpp,512,270};
  WritableBuffer dst{out.data(),W,H,W*bpp,512,270};
  for(int shape=0;shape<4;shape++){
   s.shape=(Shape)shape;
   const blend::Config cfg{blend::Mode::Normal,blend::Mode::Multiply,0};
   render(input,dst,(Depth)depth,s,ramp,&p8,&p16,cfg,&direct);
   std::vector<double> times;
   for(int j=0;j<25;j++){
    auto start=std::chrono::steady_clock::now();
    if(!render(input,dst,(Depth)depth,s,ramp,&p8,&p16,cfg,&direct))
     return 1;
    auto end=std::chrono::steady_clock::now();
    times.push_back(std::chrono::duration<double,std::milli>(end-start).count());
   }
   std::sort(times.begin(),times.end());
   sinkTiles+=out[0]+out[out.size()/2]+out.back();
   const char* shapes[]={"Linear","Radial","Angle","Mapping"};
   std::cout<<(depth==0?8:depth==1?16:32)<<"bpc "<<shapes[shape]
      <<" Normal+Multiply 256x128 median_ms "<<std::fixed
      <<std::setprecision(4)<<times[12]<<" p90 "<<times[22]<<"\n";
  }
 }
 std::cout<<"checksum "<<sinkTiles<<"\n";
}
