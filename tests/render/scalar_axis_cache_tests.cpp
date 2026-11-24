#include "../../src/render/cpu/raster_core.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace gradation;
using namespace gradation::raster;
#if defined(__AVX2__)
#error This test intentionally uses the portable non-AVX2 implementation.
#endif
template<class P>bool run(int w,int h,bool bench){
 const int pitch=(w+7)*sizeof(P);
 std::vector<uint8_t> input(size_t(pitch)*h),a(input.size(),0xa5),b(a);
 uint32_t seed=19;auto rand=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  float alpha=rand(),r=rand()*alpha,g=rand()*alpha,bl=rand()*alpha;
  P p{};
  if constexpr(sizeof(P)==4)p={u8(alpha),u8(r),u8(g),u8(bl)};
  else if constexpr(sizeof(P)==8)p={uint16_t(alpha*32768+.5f),uint16_t(r*32768+.5f),uint16_t(g*32768+.5f),uint16_t(bl*32768+.5f)};
  else{
   if((x+y)%19==0)alpha=0;
   if((x+y)%23==0)r=-.25f;
   if((x+y)%31==0)g=2.5f;
   if((x+y)%37==0)bl=std::numeric_limits<float>::quiet_NaN();
   p={alpha,r,g,bl};
  }
  std::memcpy(input.data()+size_t(y)*pitch+size_t(x)*sizeof(P),&p,sizeof(P));
 }
 Settings s{};s.startX=15.25f;s.startY=9.5f;
 const std::vector<Stop> stops={{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}};
 Gradient g(s,stops);Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 const DirectRamp direct{stops[0].color,stops[2].color,stops[1].color,.41f,3};
 int cases=0;
 for(int axis=0;axis<2;axis++)for(int rev=0;rev<(bench?1:2);rev++)
 for(int tr=0;tr<(bench?1:4);tr++)for(int dm=0;dm<(bench?1:2);dm++){
  s.endX=s.startX+(axis?0.f:w*.8f);s.endY=s.startY+(axis?h*.8f:0.f);
  s.reflect=tr==1;s.loop=tr==2;s.invert=tr==3;s.offsetPx=tr*11.4f;
  const int stride=rev?-pitch:pitch;const size_t shift=rev?size_t(pitch)*(h-1):0;
  Buffer src{input.data()+shift,w,h,stride,-13,17};
  WritableBuffer da{a.data()+shift,w,h,stride,-13,17},db{b.data()+shift,w,h,stride,-13,17};
  for(int color=0;color<blend::ColorChoices;color++)for(int alpha:{0,1,2,3,5,28}){
   if(bench&&!(alpha==1&&(color==1||color==12||color==18||color==29)))continue;
   const blend::Config cfg{blend::Mode(color),blend::Mode(alpha),51};
   auto ref=[&]{renderComposite<P,Shape::Linear,false>(src,da,s,ramp,cfg,dm?&direct:nullptr);};
   auto opt=[&]{renderComposite<P,Shape::Linear,true>(src,db,s,ramp,cfg,dm?&direct:nullptr);};
   ref();opt();
   if(a!=b){std::cerr<<"FAIL scalar cache "<<sizeof(P)<<" "<<axis<<" "<<rev<<" "<<tr<<" "<<color<<" "<<alpha<<" "<<dm<<"\n";return false;}
   cases++;
   if(bench){
    std::array<double,5> before{},after{};
    for(int k=0;k<5;k++){
     auto measure=[](auto fn){auto t=std::chrono::steady_clock::now();fn();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();};
     if(k%2){after[k]=measure(opt);before[k]=measure(ref);}else{before[k]=measure(ref);after[k]=measure(opt);}
    }
    std::sort(before.begin(),before.end());std::sort(after.begin(),after.end());
    std::cout<<"bench,"<<sizeof(P)*2<<","<<axis<<","<<color<<","<<before[2]<<","<<after[2]<<"\n";
   }
  }
 }
 std::cout<<"PASS scalar axis "<<sizeof(P)*2<<"bpc "<<cases<<" cases, exact bytes/padding\n";
 return true;
}
int main(){
 if(std::getenv("GRADATION_AXIS_BENCH"))
  return run<Pixel16>(1920,1080,true)&&run<Pixel32>(1920,1080,true)?0:1;
 return run<Pixel8>(129,17,false)&&run<Pixel16>(129,17,false)&&run<Pixel32>(129,17,false)?0:1;
}
