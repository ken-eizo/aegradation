#include "../../src/render/cpu/raster_angle_cache.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
using namespace gradation;
using namespace gradation::raster;
template<class P> bool test(Depth depth){
 constexpr int w=1920,h=1080,pitch=(w+8)*sizeof(P);
 std::vector<uint8_t> input(size_t(pitch)*h),out(input.size(),0xa5),ref(out);
 uint32_t seed=17;
 auto changeInput=[&]{
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   seed=seed*1664525u+1013904223u;
   P px{};
   if constexpr(sizeof(P)==4)px.a=(uint8_t)(seed>>24);
   else px.a=(uint16_t)(seed>>16);
   std::memcpy(input.data()+size_t(y)*pitch+x*sizeof(P),&px,sizeof(P));
  }
 };
 changeInput();
 Buffer src{input.data(),w,h,pitch,7,11};
 WritableBuffer dst{out.data(),w,h,pitch,7,11},baseline{ref.data(),w,h,pitch,7,11};
 const Stop stops[]={{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}};
 auto entry=lutcache::global().get(lutcache::makeKey(Space::SRGB,stops,3));
 auto& ramp=entry->preparedRamp();
 auto p8=entry->getPacked8();auto p16=entry->getPacked16();
 Settings s{};s.shape=Shape::Angle;s.startX=960.5f;s.startY=540.5f;s.endX=1450.3f;s.endY=800.9f;
 const blend::Config cfg{blend::Mode::Replace,blend::Mode::Multiply,0};
 anglecache::Cache cache;
 auto base=[&]{return parallel::render(src,baseline,depth,s,ramp,p8,p16,cfg);};
 auto fast=[&]{return anglecache::render(cache,entry,src,dst,depth,s,ramp,p8,p16,cfg);};
 for(int k=0;k<3;k++){
  s.loop=k==1;s.reflect=k==2;s.invert=k==2;s.offsetPx=k*13.7f;
  if(fast()||fast()||!base())return false;
  auto start=std::chrono::steady_clock::now();
  const bool rendered=fast();
  const auto cold=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(!rendered||ref!=out){
   std::cerr<<"parity failure "<<sizeof(P)<<" mode "<<k<<"\n";return false;
  }
  std::cout<<"mode "<<k<<" cold build+compose "<<cold<<"ms\n";
  changeInput();
  if(!base()||!fast()||ref!=out){std::cerr<<"animated alpha parity failure\n";return false;}
  for(int cached:{0,1}){
   std::array<double,15> ms{};
   for(auto& t:ms){
    auto a=std::chrono::steady_clock::now();
    if(!(cached?fast():base()))return false;
    t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();
   }
   std::sort(ms.begin(),ms.end());
   std::cout<<sizeof(P)*2<<"bpc mode="<<k<<" "<<(cached?"cached":"baseline")<<" median="<<std::fixed<<std::setprecision(4)<<ms[7]<<"ms\n";
  }
 }
 // The key owns a LUT identity, so new stop colors invalidate old pixels.
 const Stop changed[]={{0,{1.f,.2f,.1f,.9f}},{1,{0.f,.3f,.8f,.45f}}};
 auto other=lutcache::global().get(lutcache::makeKey(Space::SRGB,changed,2));
 const auto& ramp2=other->preparedRamp();const auto* packed2=other->getPacked8();
 auto newColor=[&]{return anglecache::render(cache,other,src,dst,depth,s,ramp2,packed2,nullptr,cfg);};
 if(newColor()||newColor()||!newColor())return false;
 if(!parallel::render(src,baseline,depth,s,ramp2,packed2,nullptr,cfg)||ref!=out)return false;
 // Independent outputs in simultaneous callers. Contention can fall back
 // but must not block, corrupt or return stale pixels.
 std::atomic<bool> ok{true};
 auto worker=[&]{
  std::vector<uint8_t> local(out.size(),0xa5);
  WritableBuffer tile{local.data(),w,h,pitch,7,11};
  for(int i=0;i<4;i++){
   if(!anglecache::render(cache,other,src,tile,depth,s,ramp2,packed2,nullptr,cfg)&&
      !parallel::render(src,tile,depth,s,ramp2,packed2,nullptr,cfg)){ok=false;return;}
   if(local!=ref){ok=false;return;}
  }
 };
 std::thread a(worker),b(worker);worker();a.join();b.join();
 if(!ok)return false;
 // Noneligible depths or cropped source never enter the cache.
 if(anglecache::render(cache,other,src,dst,Depth::U16,s,ramp2,packed2,nullptr,cfg))return false;
 Buffer cropped=src;cropped.width--;
 if(anglecache::render(cache,other,cropped,dst,depth,s,ramp2,packed2,nullptr,cfg))return false;
 return true;
}
int main(){if(!test<Pixel8>(Depth::U8))return 2;
 std::cout<<"PASS repeated Angle geometry, changed source alpha, all 3 transforms, padded ROI, full byte parity\n";
}
