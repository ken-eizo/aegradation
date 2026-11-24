#include "../../src/render/cpu/raster_parallel.hpp"
#include <iostream>
using namespace gradation;
using namespace gradation::raster;
template<class P,Shape S> bool parity(){
 constexpr int w=257,h=9,pitch=(w+5)*sizeof(P);
 std::vector<uint8_t> src(size_t(pitch)*h),a(src.size(),0xa5),b(a);
 uint32_t seed=17;
 auto r=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  float al=r(),red=r()*al,g=r()*al,bl=r()*al;
  P v{};
  if constexpr(sizeof(P)==4)v={u8(al),u8(red),u8(g),u8(bl)};
  else if constexpr(sizeof(P)==8)v={uint16_t(al*32768+.5f),uint16_t(red*32768+.5f),uint16_t(g*32768+.5f),uint16_t(bl*32768+.5f)};
  else v={al,red,g,bl};
  std::memcpy(src.data()+size_t(y)*pitch+x*sizeof(P),&v,sizeof(P));
 }
 Settings s{};s.shape=S;s.startX=39.5f;s.startY=4.5f;s.endX=190.5f;s.endY=9.4f;
 Gradient g(s,{{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}});
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 DirectRamp direct{};direct.first={.2f,.4f,.8f,.35f};direct.last={.1f,.7f,.3f,.98f};direct.middle={.91f,.32f,.22f,.7f};direct.count=3;direct.middlePosition=.41f;
 const blend::Config modes[]={{blend::Mode::Normal,blend::Mode::Normal,0},{blend::Mode::Screen,blend::Mode::Multiply,0},{blend::Mode::SoftLight,blend::Mode::Multiply,0},{blend::Mode::Hue,blend::Mode::Normal,0}};
 float dx=s.endX-s.startX,dy=s.endY-s.startY,inv=1.f/std::max(std::hypot(dx,dy),1.e-6f),angle=fastAtan2(dy,dx);
 WritableBuffer da{a.data(),w,h,pitch,-7,11},db{b.data(),w,h,pitch,-7,11};
 for(int tr=0;tr<3;tr++){
  s.loop=tr==1;s.reflect=tr==2;s.invert=tr==2;
  for(auto cfg:modes)for(const auto* dr:{(const DirectRamp*)nullptr,(const DirectRamp*)&direct}){
   for(int y=0;y<h;y++){
    auto* ar=a.data()+size_t(y)*pitch;auto* br=b.data()+size_t(y)*pitch;
    auto* in=src.data()+size_t(y)*pitch;
    for(auto span:{std::pair<int,int>{0,7},{7,w-5},{w-5,w}}){
     const auto* input=span.first==7?in+size_t(span.first)*sizeof(P):nullptr;
     simd::blendRow8Impl<P,S>(ar,input,da,y,s,ramp,cfg,inv,dx,dy,angle,1.f,.13f,span.first,span.second,dr);
     simd::blendRow8<P,S>(br,input,db,y,s,ramp,cfg,inv,dx,dy,angle,1.f,.13f,span.first,span.second,dr);
    }
   }
   if(a!=b){std::cerr<<"graph parity failed "<<sizeof(P)<<" "<<int(S)<<" "<<int(cfg.color)<<" "<<tr<<"\n";return false;}
  }
 }
 return true;
}
template<class P>bool shapes(){return parity<P,Shape::Linear>()&&parity<P,Shape::Radial>()&&parity<P,Shape::Angle>()&&parity<P,Shape::Mapping>();}
template<class P>bool axisParity(){
 constexpr int w=256,h=32,pitch=(w+5)*sizeof(P);
 std::vector<uint8_t> src(size_t(pitch)*h),a(src.size(),0xa5),b(a);
 uint32_t seed=17;
 auto r=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  float al=r(),red=r()*al,g=r()*al,bl=r()*al;
  P v{};
  if constexpr(sizeof(P)==8)v={uint16_t(al*32768+.5f),uint16_t(red*32768+.5f),uint16_t(g*32768+.5f),uint16_t(bl*32768+.5f)};
  else v={al,red,g,bl};
  std::memcpy(src.data()+size_t(y)*pitch+x*sizeof(P),&v,sizeof(P));
 }
 Ramp ramp{};Settings settings{};
 Gradient g(settings,{{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}});
 for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 DirectRamp direct{};direct.first={.2f,.4f,.8f,.35f};direct.last={.1f,.7f,.3f,.98f};direct.middle={.91f,.32f,.22f,.7f};direct.count=3;direct.middlePosition=.41f;
 for(int axis=0;axis<2;axis++)for(int reverse=0;reverse<2;reverse++)
 for(int tr=0;tr<4;tr++)for(int color=0;color<=32;color++)for(int alpha:{0,1,5})
 for(auto* dr:{(const DirectRamp*)nullptr,(const DirectRamp*)&direct}){
  Settings s{};s.startX=17.5f;s.startY=11.5f;
  s.endX=s.startX+(axis?0.f:130.f);s.endY=s.startY+(axis?15.f:0.f);
  s.loop=tr==1;s.reflect=tr==2;s.invert=tr==3;s.offsetPx=tr?-.13f:0.f;
  const blend::Config cfg{(blend::Mode)color,(blend::Mode)alpha,42};
  const int stride=reverse?-pitch:pitch;
  const size_t origin=reverse?size_t(h-1)*pitch:0;
  const Buffer input{src.data()+origin,w,h,stride,-7,11};
  WritableBuffer da{a.data()+origin,w,h,stride,-7,11},db{b.data()+origin,w,h,stride,-7,11};
  const float dx=s.endX-s.startX,dy=s.endY-s.startY,len=std::max(std::hypot(dx,dy),1.e-6f),angle=fastAtan2(dy,dx),invScale=100.f/std::max(s.scalePercent,.0001f),off=s.offsetPx/len;
  if(!renderAxisComposite<P>(input,da,s,ramp,cfg,dr,len,dx,dy,angle,invScale,off,0,0))return false;
  for(int y=0;y<h;y++){
   auto* out=db.data+std::ptrdiff_t(y)*stride;
   const auto* in=input.data+std::ptrdiff_t(y)*stride;
   simd::blendRow8<P,Shape::Linear>(out,in,db,y,s,ramp,cfg,1.f/len,dx,dy,angle,invScale,off,0,w,dr);
  }
  if(a!=b){std::cerr<<"axis cache mismatch depth="<<sizeof(P)<<" axis="<<axis<<" mode="<<color<<" alpha="<<alpha<<" transform="<<tr<<"\n";return false;}
 }
 return true;
}
bool normalAoSParity(){
 using namespace simd;
 alignas(32) Pixel32 source[8],reference[8],actual[8];
 uint32_t rng=17;
 auto rand=[&](){rng=rng*1664525u+1013904223u;return float(rng>>8)/16777216.f;};
 constexpr blend::Config cfg{blend::Mode::Normal,blend::Mode::Normal,0};
 const float edge[]={0.f,-0.f,1.f,-1.f,.5f,1.e-11f,2.f,1.e30f,
                    std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
 for(int k=0;k<20000;++k){
  alignas(32) float a[8],r[8],g[8],b[8];
  for(int i=0;i<8;++i){
   auto f=[&](){return k<10000?rand()*4.f-1.5f:edge[(rng=rng*1664525u+1013904223u)%10];};
   source[i]={f(),f(),f(),f()};
   a[i]=rand();r[i]=rand()*2.f-1.f;g[i]=rand()*2.f-1.f;b[i]=rand()*2.f-1.f;
  }
  Channels fg{_mm256_load_ps(a),_mm256_load_ps(r),_mm256_load_ps(g),_mm256_load_ps(b)};
  const auto* input=k%7?reinterpret_cast<const uint8_t*>(source):nullptr;
  Channels src=input?source32(input):Channels{};
  store32(reinterpret_cast<uint8_t*>(reference),composite8<int(blend::Mode::Normal),int(blend::Mode::Normal)>(src,fg,cfg,0,0));
  store32NormalAoS(reinterpret_cast<uint8_t*>(actual),input,fg);
  if(std::memcmp(actual,reference,sizeof(actual))!=0){
   std::cerr<<"Normal AoS exact parity failure case "<<k<<"\n";return false;
  }
 }
 std::cout<<"PASS 20000 Normal AoS blocks, HDR/NaN/Inf/negative/absent source exact parity\n";
 return true;
}
int main(){if(!normalAoSParity())return 3;
 if(!shapes<Pixel8>()||!shapes<Pixel16>()||!shapes<Pixel32>())return 1;
 if(!axisParity<Pixel16>()||!axisParity<Pixel32>())return 2;
 std::cout<<"PASS axis cache exact parity: 6336 high-depth cases, both axes, reverse strides, all RGB modes, transforms and LUT/direct\n";
 std::cout<<"PASS 288 AOT blend graphs, three depths, all shapes, transform modes, direct/LUT, odd ROI and outside source\n";}
