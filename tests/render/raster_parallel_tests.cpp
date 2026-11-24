#include "../../src/render/cpu/raster_parallel.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int W=1024,H=1024;
 const DirectRamp direct{{.21f,.82f,.13f,.47f},
                         {.93f,.12f,.95f,.98f}};
 Settings initial{};
 initial.startX=97.2f;initial.startY=337.5f;
 initial.endX=847.8f;initial.endY=702.3f;
 Gradient g(initial,{{0,direct.first},{1,direct.last}});
 Ramp ramp{};for(int j=0;j<=4096;j++)ramp[(size_t)j]=g.sample(j/4096.f);
 const std::array<blend::Config,3> configs{{
   {blend::Mode::Replace,blend::Mode::Replace,0},
   {blend::Mode::Replace,blend::Mode::Multiply,0},
   {blend::Mode::Normal,blend::Mode::Multiply,0}
 }};
 std::vector<uint8_t> src((size_t)W*H*16),
                      expected(src.size()),actual(src.size());
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const float a=((x+7*y)%17==0)?.3f:1.f;
  const Pixel32 p{a,(float)((x*13+y)%251)/250.f*a,
      (float)((y*41+x)%239)/238.f*a,
      ((x+y)%29==0?1.5f:.65f)*a};
  std::memcpy(src.data()+((size_t)y*W+x)*16,&p,16);
 }
 int cases=0;
 for(int shape=0;shape<4;shape++)for(int mode=0;mode<3;mode++){
  if(shape==3&&mode==0)continue;
  Settings s=initial;s.shape=(Shape)shape;
  const blend::Config cfg=configs[(size_t)mode];
  for(int crop=0;crop<2;crop++){
   const Buffer in=mode==0?Buffer{}:
     Buffer{src.data(),W-crop*17,H-crop*11,W*16,crop?9:0,crop?7:0};
   WritableBuffer first{expected.data(),W,H,W*16,0,0};
   WritableBuffer second{actual.data(),W,H,W*16,0,0};
   std::fill(expected.begin(),expected.end(),0xcd);
   std::fill(actual.begin(),actual.end(),0xba);
   if(!gradation::raster::render(in,first,Depth::F32,s,ramp,nullptr,nullptr,cfg,&direct)||
      !gradation::raster::parallel::render(in,second,Depth::F32,s,ramp,nullptr,nullptr,cfg,&direct)){
     std::cerr<<"Failed render shape "<<shape<<" mode "<<mode<<"\n";return 2;
   }
   if(std::memcmp(expected.data(),actual.data(),actual.size())!=0){
     for(size_t i=0;i<actual.size();i++)if(expected[i]!=actual[i]){
      std::cerr<<"Parallel pixel mismatch shape="<<shape<<" mode="<<mode
       <<" crop="<<crop<<" byteOffset="<<i<<"\n";break;
     }
     return 3;
   }
   cases++;
  }
 }
 // Full-resolution simultaneous independent MFR render invocations. One
 // caller may claim the CPU budget; all other callers must safely process
 // serially with no output corruption, memory overlap or global data race.
 std::atomic<int> failures{0};
 std::array<std::thread,4> workers{};
 for(int k=0;k<4;k++)workers[(size_t)k]=std::thread([&,k]{
  Settings s=initial;s.shape=(Shape)k;
  s.startX+=(float)k*3.7f;
  s.endY-=(float)k*2.6f;
  const blend::Config cfg{blend::Mode::Normal,blend::Mode::Multiply,0};
  Buffer input{src.data(),W,H,W*16,0,0};
  std::vector<uint8_t> serial(expected.size()),other(expected.size());
  WritableBuffer dst0{serial.data(),W,H,W*16,0,0};
  WritableBuffer dst1{other.data(),W,H,W*16,0,0};
  if(!gradation::raster::render(input,dst0,Depth::F32,s,ramp,nullptr,nullptr,cfg,&direct)||
     !gradation::raster::parallel::render(input,dst1,Depth::F32,s,ramp,nullptr,nullptr,cfg,&direct)||
     std::memcmp(serial.data(),other.data(),serial.size())!=0)
   failures.fetch_add(1);
 });
 for(auto& w:workers)w.join();
 if(failures.load()){std::cerr<<"MFR concurrency error\n";return 4;}
 std::cout<<"PASS "<<cases<<" full-frame 32bpc cases (4 shapes, 3 blends,"
   <<" cropped input), 4 concurrent heavy MFR instances, byte-identical output\n";
}
