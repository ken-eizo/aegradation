#include "../src/core/gradient_core.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
using namespace gradation;
int main(){
 constexpr int W=1920,H=1080;
 Settings s;s.shape=Shape::Linear;s.startX=0;s.startY=0;s.endX=W;s.endY=0;
 Gradient g(s);
 std::array<Color,4097> lut;
 auto begin=std::chrono::steady_clock::now();
 for(int i=0;i<=4096;i++)lut[i]=g.sample(i/4096.f);
 auto done=std::chrono::steady_clock::now();
 volatile uint64_t checksum=0;
 auto renderStart=std::chrono::steady_clock::now();
 for(int frame=0;frame<8;frame++){
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const float t=(float)x/W*4096.f;
   const int idx=std::min(4095,(int)t);
   const float f=t-idx;
   const float red=lut[idx].r+(lut[idx+1].r-lut[idx].r)*f;
   checksum+=(uint64_t)(red*255.f);
  }
 }
 auto end=std::chrono::steady_clock::now();
 std::cout<<"lut_ms="<<std::chrono::duration<double,std::milli>(done-begin).count()
 <<" fullhd_core_ms_per_frame="<<std::chrono::duration<double,std::milli>(end-renderStart).count()/8
 <<" checksum="<<checksum<<"\n";
}
