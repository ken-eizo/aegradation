#include "raster_core.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile uint64_t checksumRows=0;
int main(){
 Settings s{};s.shape=Shape::Mapping;
 Gradient g(s,{{0,{.2f,.8f,.3f,1.f}},{.5f,{.8f,.1f,.4f,.5f}},
               {1,{.1f,.2f,.9f,1.f}}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 const PackedRamp8 a=makePacked8(ramp);
 const PackedRamp16 b=makePacked16(ramp);
 for(int width:{1920,3840}){
  const int height=width==1920?1080:2160;
  for(int dep=0;dep<3;dep++){
   const int bpp=dep==0?4:dep==1?8:16;
   const int stride=width*bpp;
   std::vector<uint8_t> input((size_t)stride*height),output(input.size());
   for(int y=0;y<height;y++)for(int x=0;x<width;x++){
    const float t=(float)(x%173)/172.f;
    const float al=.5f+.5f*t;
    auto* p=input.data()+(size_t)y*stride+(size_t)x*bpp;
    if(dep==0){Pixel8 v{u8(al),u8(al*t),u8(al*.35f),u8(al*.2f)};
      std::memcpy(p,&v,4);}
    if(dep==1){Pixel16 v{(uint16_t)(al*32768),
      (uint16_t)(al*t*32768),(uint16_t)(al*.35f*32768),
      (uint16_t)(al*.2f*32768)};std::memcpy(p,&v,8);}
    if(dep==2){Pixel32 v{al,al*t,al*.35f,al*.2f};std::memcpy(p,&v,16);}
   }
   Buffer in{input.data(),width,height,stride,0,0};
   WritableBuffer dst{output.data(),width,height,stride,0,0};
   for(int pattern=0;pattern<2;pattern++){
    if(pattern==1){
     for(int y=0;y<height;y++){
      // Unique row: changing only the second pixel is enough to
      // invalidate the previous-row equality shortcut.
      input[(size_t)y*stride+bpp]=uint8_t(y%251);
     }
    }
    render(in,dst,(Depth)dep,s,ramp,&a,&b);
    std::vector<double> t;
    for(int i=0;i<5;i++){
     const auto start=std::chrono::steady_clock::now();
     if(!render(in,dst,(Depth)dep,s,ramp,&a,&b))return 2;
     const auto end=std::chrono::steady_clock::now();
     t.push_back(std::chrono::duration<double,std::milli>(end-start).count());
    }
    std::sort(t.begin(),t.end());
    checksumRows+=output[0]+output[output.size()/2]+output.back();
    std::cout<<width<<"x"<<height<<" "<<(dep==0?"8":dep==1?"16":"32")
     <<" Mapping "<<(pattern==0?"repeated input rows":"distinct input rows")
     <<" median_ms "<<std::fixed<<std::setprecision(3)<<t[2]
     <<" min_ms "<<t[0]<<"\n";
   }
  }
 }
 std::cout<<"checksum "<<checksumRows<<"\n";
}
