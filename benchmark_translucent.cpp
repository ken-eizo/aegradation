#include "raster_core.hpp"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
volatile uint64_t sink=0;
int main(){
 std::cout<<"Translucent alpha workload: mixed alpha, all four shapes, 8/16/32bpc\n";
 std::cout<<"Raster benchmark: full RGBA output, AE ARGB format, single CPU thread\n";
 std::cout<<"All times exclude LUT construction and AE host overhead.\n";
 for(auto size:{std::pair<int,int>{1920,1080},{3840,2160}}){
  int w=size.first,h=size.second;
  Settings settings;
  settings.startX=w*.25f;settings.startY=h*.50f;
  settings.endX=w*.75f;settings.endY=h*.50f;
  settings.space=Space::Spectral;
  std::vector<Stop> stops={{0,{1,0,0,1}},{.5f,{1,1,0,1}},{1,{.03f,.20f,1,1}}};
  auto beginPrep=std::chrono::steady_clock::now();
  Gradient gradient(settings,stops);
  Ramp lut{};
  for(int i=0;i<=4096;i+=4)lut[i]=gradient.sample(i/4096.f);
  for(int i=0;i<4096;i+=4)for(int k=1;k<4;k++){
   float t=k/4.f;const auto&a=lut[i];const auto&b=lut[i+4];
   lut[i+k]={a.r+t*(b.r-a.r),a.g+t*(b.g-a.g),a.b+t*(b.b-a.b),a.a+t*(b.a-a.a)};
  }
  const auto packed=makePacked8(lut);const auto packed16=makePacked16(lut);
  auto prepEnd=std::chrono::steady_clock::now();
  std::cout<<"Size "<<w<<"x"<<h<<" cold spectral LUT ms="<<std::chrono::duration<double,std::milli>(prepEnd-beginPrep).count()<<"\n";
  for(Depth depth:{Depth::U8,Depth::U16,Depth::F32}){
   int bpp=depth==Depth::U8?4:(depth==Depth::U16?8:16);
   size_t bytes=(size_t)w*h*bpp;
   std::vector<uint8_t> input(bytes),output(bytes);
   for(int y=0;y<h;y++)for(int x=0;x<w;x++){
    uint8_t* ptr=input.data()+((size_t)y*w+x)*bpp;
    if(depth==Depth::U8){
      Pixel8 v{(uint8_t)(48+(x*3+y*7)%208),(uint8_t)(x%256),(uint8_t)(y%256),128};
      std::memcpy(ptr,&v,bpp);
    }else if(depth==Depth::U16){
      Pixel16 v{(uint16_t)(4096+(x*17+y*29)%28673),(uint16_t)((x%256)*128),(uint16_t)((y%256)*128),16384};
      std::memcpy(ptr,&v,bpp);
    }else{
      Pixel32 v{(48+(x*3+y*7)%208)/255.f,(x%256)/255.f,(y%256)/255.f,.5f};
      std::memcpy(ptr,&v,bpp);
    }
   }
   Buffer src{input.data(),w,h,w*bpp,0,0};
   WritableBuffer dst{output.data(),w,h,w*bpp,0,0};
   const char* depthname=depth==Depth::U8?"8bpc":depth==Depth::U16?"16bpc":"32bpc";
   for(Shape shape:{Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping}){
    settings.shape=shape;
    render(src,dst,depth,settings,lut,&packed,&packed16);
    std::vector<double> times;
    for(int j=0;j<7;j++){
     auto start=std::chrono::steady_clock::now();
     render(src,dst,depth,settings,lut,&packed,&packed16);
     auto end=std::chrono::steady_clock::now();
     times.push_back(std::chrono::duration<double,std::milli>(end-start).count());
    }
    std::sort(times.begin(),times.end());
    sink+=output[0]+output[bytes/2]+output.back();
    const char* shapeName=shape==Shape::Linear?"Linear":shape==Shape::Radial?"Radial":
      shape==Shape::Angle?"Angle":"Mapping";
    std::cout<<depthname<<" "<<shapeName<<" median="<<times[3]<<"ms best="<<times[0]<<"ms\n";
   }
  }
 }
 std::cout<<"checksum="<<sink<<"\n";
}
