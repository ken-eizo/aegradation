#include "../../src/render/cpu/raster_parallel.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <iomanip>
using namespace gradation;
using namespace gradation::raster;
int main(int argc,char** argv){
 // Check bit parity across ordinary and center rows, including signed zero.
 alignas(32) float xv[8],a[8],b[8];
 for(float y:{-12345.f,-100.f,-1.f,-.5f,-1.e-21f,-0.f,0.f,1.e-21f,.5f,1.f,100.f,12345.f}){
  for(int j=-2048;j<=2048;j+=8){
   for(int i=0;i<8;i++)xv[i]=(j+i)*.125f;
   auto x=_mm256_load_ps(xv);
   _mm256_store_ps(a,simd::vatan2(_mm256_set1_ps(y),x));
   _mm256_store_ps(b,simd::vatan2Row(y,x));
   if(std::memcmp(a,b,sizeof(a)))return 2;
  }
 }
 constexpr int w=1923,h=1080;
 Settings s{};s.shape=Shape::Angle;s.startX=960.f;s.startY=540.f;s.endX=1450.f;s.endY=800.f;
 Gradient g(s,{{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}});
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 auto p8=makePacked8(ramp);auto p16=makePacked16(ramp);
 std::ofstream binary;if(argc>1){binary.open(argv[1],std::ios::binary);if(!binary)return 3;}
 for(auto depth:{Depth::U8,Depth::U16,Depth::F32}){
  int bytes=depth==Depth::U8?4:depth==Depth::U16?8:16,pitch=(w+5)*bytes;
  std::vector<uint8_t> out(size_t(pitch)*h,0xA5);
  WritableBuffer dst{out.data(),w,h,pitch,7,11};
  for(int mode=0;mode<3;mode++){
   s.loop=mode==1;s.reflect=mode==2;s.invert=mode==2;s.offsetPx=mode*37.4f;
   const blend::Config cfg{blend::Mode::Replace,blend::Mode::Replace,0};
   auto run=[&]{return render(Buffer{},dst,depth,s,ramp,&p8,&p16,cfg);};
   if(!run()||!run())return 4;
   std::array<double,13> ms{};
   for(auto& t:ms){auto a=std::chrono::steady_clock::now();if(!run())return 5;t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();}
   std::sort(ms.begin(),ms.end());
   if(binary.is_open())binary.write((const char*)out.data(),out.size());
   for(int y=0;y<h;y++)for(int x=w*bytes;x<pitch;x++)if(out[size_t(y)*pitch+x]!=0xA5)return 6;
   std::cout<<bytes*2<<"bpc mode="<<mode<<" median="<<std::fixed<<std::setprecision(4)<<ms[6]<<"ms\n";
  }
 }
 if(binary.is_open()&&!binary)return 7;
}
