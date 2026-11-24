#include "../../src/render/cpu/raster_parallel.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <iomanip>
using namespace gradation;
using namespace gradation::raster;
int main(int argc,char** argv){
 constexpr int w=1923,h=1080,pitch=(w+5)*16;
 std::vector<uint8_t> src((size_t)pitch*h),out(src.size(),0xA5);
 uint32_t seed=32768;
 auto rnd=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  float a=rnd(); Pixel32 v{a,rnd()*a,rnd()*a,rnd()*a};
  std::memcpy(src.data()+size_t(y)*pitch+x*16,&v,16);
 }
 Settings s{};s.startX=137.4f;s.startY=251.9f;s.endX=1431.7f;s.endY=866.1f;
 Gradient g(s,{{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}});
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 Buffer in{src.data(),w,h,pitch,7,11};
 WritableBuffer dst{out.data(),w,h,pitch,7,11};
 std::ofstream binary;if(argc>1){binary.open(argv[1],std::ios::binary);if(!binary)return 3;}
 for(int k=0;k<6;k++){
  s.shape=(Shape)std::min(k,3);s.loop=k==4;s.reflect=k==5;s.invert=k==5;
  auto mode=blend::Config{blend::Mode::Replace,blend::Mode::Multiply,0};
  auto run=[&]{return render(in,dst,Depth::F32,s,ramp,nullptr,nullptr,mode);};
  if(!run()||!run())return 2;
  std::array<double,11> ms{};
  for(auto& t:ms){auto a=std::chrono::steady_clock::now();if(!run())return 2;t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();}
  std::sort(ms.begin(),ms.end());
  uint64_t hash=14695981039346656037ull;
  for(auto c:out){hash^=c;hash*=1099511628211ull;}
  for(int y=0;y<h;y++)for(int x=w*16;x<pitch;x++)if(out[size_t(y)*pitch+x]!=0xA5)return 4;
  if(binary.is_open())binary.write((const char*)out.data(),out.size());
  std::cout<<k<<" hash="<<std::hex<<hash<<std::dec<<" median="<<std::fixed<<std::setprecision(4)<<ms[5]<<"ms\n";
 }
 if(binary.is_open()&&!binary)return 5;
}
