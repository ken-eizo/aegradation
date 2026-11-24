#include "../../src/render/cpu/raster_core.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <fstream>
using namespace gradation;
using namespace gradation::raster;
int main(int argc,char** argv){
 constexpr int w=1923,h=1080,pitch=(w+5)*8;
 std::vector<uint8_t> src((size_t)pitch*h),out(src.size(),0xA5);
 uint32_t seed=32768;
 auto rnd=[&]{seed=seed*1664525u+1013904223u;return uint16_t(seed>>16);};
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  Pixel16 v{rnd(),rnd(),rnd(),rnd()};
  std::memcpy(src.data()+size_t(y)*pitch+x*8,&v,8);
 }
 Settings s{};
 Gradient g(s,{{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}});
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 auto lut=makePacked16(ramp);
 std::ofstream binary;
 if(argc>1){binary.open(argv[1],std::ios::binary);if(!binary)return 3;}
 for(int k=0;k<3;k++){
  s.loop=k==1;s.reflect=k==2;s.invert=k==2;
  auto run=[&]{for(int y=0;y<h;y++)simd::mapping16(out.data()+size_t(y)*pitch,src.data()+size_t(y)*pitch,w,s,lut,1.0f,k*.13f);};
  run();run();
  std::array<double,15> ms{};
  for(auto& t:ms){const auto a=std::chrono::steady_clock::now();run();t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();}
  std::sort(ms.begin(),ms.end());
  uint64_t hash=14695981039346656037ull;
  for(auto c:out){hash^=c;hash*=1099511628211ull;}
  for(int y=0;y<h;y++)for(int x=w*8;x<pitch;x++)if(out[size_t(y)*pitch+x]!=0xA5)return 2;
  if(binary.is_open())binary.write((const char*)out.data(),out.size());
  std::cout<<k<<" hash="<<std::hex<<hash<<std::dec<<" median="<<std::fixed<<std::setprecision(4)<<ms[7]<<"ms\n";
 }
 if(binary.is_open()&&!binary)return 4;
}
