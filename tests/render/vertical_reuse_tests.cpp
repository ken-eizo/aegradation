#include "../../src/render/cpu/raster_parallel.hpp"
#include <chrono>
#include <iostream>
#include <cstring>
#include <iomanip>
using namespace gradation;
using namespace gradation::raster;
template<class P> bool test(Depth depth){
 constexpr int w=1920,h=1080;
 constexpr int pitch=(w+8)*sizeof(P);
 std::vector<uint8_t> in((size_t)pitch*h),out(in.size()),ref(in.size());
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  P v{};
  if constexpr(sizeof(P)==4)v.a=(uint8_t)((x+y)%13?255:(x+y)%256);
  else if constexpr(sizeof(P)==8)v.a=(uint16_t)((x+y)%13?32768:(x*37+y*11)%32769);
  else v.a=(x+y)%13?1.f:.41f;
  std::memcpy(in.data()+size_t(y)*pitch+size_t(x)*sizeof(P),&v,sizeof(P));
 }
 Settings s{};s.startX=s.endX=450.3f;s.startY=215.4f;s.endY=870.7f;
 Buffer src{in.data(),w,h,pitch,7,11};
 WritableBuffer dst{out.data(),w,h,pitch,7,11};
 for(int mode=0;mode<3;mode++){
  s.loop=mode==1;s.reflect=mode==2;s.invert=mode==2;s.offsetPx=mode*13.3f;
  Gradient g(s,{{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}});
  Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
  auto p8=makePacked8(ramp);auto p16=makePacked16(ramp);
  for(auto alpha:{blend::Mode::Replace,blend::Mode::Multiply}){
   if(sizeof(P)==16&&alpha==blend::Mode::Multiply)continue;
   blend::Config cfg{blend::Mode::Replace,alpha,0};
   // Reference uses the unmodified eight-pixel SIMD primitives on every block.
   const float dy=s.endY-s.startY,invLen=1.f/std::max(std::hypot(0.f,dy),1.e-6f);
   const float invScale=100.f/std::max(s.scalePercent,.0001f),offset=s.offsetPx*invLen;
   WritableBuffer reference{ref.data(),w,h,pitch,7,11};
   for(int y=0;y<h;y++){
    if(alpha==blend::Mode::Replace)
     simd::opaqueRow<P,Shape::Linear>(reference,y,s,ramp,&p8,&p16,invLen,0.f,dy,0.f,invScale,offset);
    else
     simd::coveredRow<P,Shape::Linear>(ref.data()+size_t(y)*pitch,in.data()+size_t(y)*pitch,y,reference,s,ramp,&p8,&p16,invLen,0.f,dy,0.f,invScale,offset);
   }
   auto run=[&](bool parallel){
    auto input=alpha==blend::Mode::Replace?Buffer{}:src;
    return parallel?gradation::raster::parallel::render(input,dst,depth,s,ramp,&p8,&p16,cfg):
      render(input,dst,depth,s,ramp,&p8,&p16,cfg);
   };
   for(bool parallel:{false,true}){
    if(!run(parallel)||std::memcmp(ref.data(),out.data(),out.size()))return false;
    if(mode==0){
     std::array<double,15> ms{};
     for(auto& t:ms){auto a=std::chrono::steady_clock::now();if(!run(parallel))return false;
      t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();}
     std::sort(ms.begin(),ms.end());
     std::cout<<sizeof(P)*2<<"bpc "<<(alpha==blend::Mode::Replace?"generator":"multiply")<<" "<<(parallel?"parallel":"serial")<<" "<<std::fixed<<std::setprecision(4)<<ms[7]<<"ms\n";
    }
   }
  }
 }
 return true;
}
int main(){
 if(!test<Pixel8>(Depth::U8)||!test<Pixel16>(Depth::U16)||!test<Pixel32>(Depth::F32))return 2;
 std::cout<<"PASS vertical exact SIMD reference, padded ROI, 3 transforms, serial/parallel\n";
}
