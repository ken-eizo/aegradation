#include "../../src/render/cpu/raster_core.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int W=240,H=128;
 Settings s;s.space=Space::Spectral;s.startX=13;s.startY=17;s.endX=190;s.endY=107;
 Gradient g(s,{{0,{1,0,0,1}},{.42f,{0,1,0,.6f}},{1,{0,0,1,1}}});
 Ramp ramp;
 for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 auto packed8=makePacked8(ramp);
 auto packed16=makePacked16(ramp);
 std::vector<Pixel8> src(W*H);
 for(int i=0;i<W*H;i++)src[i]={uint8_t(i%7?255:128),uint8_t(i%251),uint8_t((i*3)%253),uint8_t((i*7)%241)};
 Buffer in{(const uint8_t*)src.data(),W,H,W*4,0,0};
 std::vector<Pixel8> reference(W*H);
 std::array<std::vector<Pixel8>,8> output;
 for(auto& v:output)v.resize(W*H);
 std::atomic<int> failure{0};
 for(int shape=0;shape<4;shape++){
  s.shape=(Shape)shape;
  WritableBuffer ref{(uint8_t*)reference.data(),W,H,W*4,0,0};
  render(in,ref,Depth::U8,s,ramp,&packed8,&packed16);
  std::vector<std::thread> threads;
  threads.reserve(output.size());
  for(auto& buf:output){
   threads.emplace_back([&buf,&in,&s,&ramp,&packed8,&packed16,&reference,&failure,W,H](){
    WritableBuffer dst{(uint8_t*)buf.data(),W,H,W*4,0,0};
    for(int iteration=0;iteration<5;iteration++){
      render(in,dst,Depth::U8,s,ramp,&packed8,&packed16);
      if(std::memcmp(buf.data(),reference.data(),reference.size()*4)!=0)failure++;
    }
   });
  }
  for(auto& th:threads)th.join();
 }
 std::cout<<"Concurrent render test 8 threads x 5 frames x 4 shapes, errors="<<failure.load()<<"\n";
 return failure.load()?1:0;
}
