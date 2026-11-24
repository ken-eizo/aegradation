#include "../../src/render/cpu/raster_geometry_cache.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int w=1920,h=1080,pitch=(w+8)*4;
 std::vector<uint8_t> input(size_t(pitch)*h),out(input.size(),0xa5),ref(out);
 uint32_t seed=31;
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  seed=seed*1664525u+1013904223u;
  input[size_t(y)*pitch+4*x]=uint8_t(seed>>24);
 }
 Buffer src{input.data(),w,h,pitch,7,11};
 WritableBuffer dst{out.data(),w,h,pitch,7,11},baseline{ref.data(),w,h,pitch,7,11};
 const blend::Config cfg{blend::Mode::Replace,blend::Mode::Multiply,0};
 Settings s{};s.startX=960.5f;s.startY=540.5f;s.endX=1450.3f;s.endY=800.9f;
 std::array<std::shared_ptr<lutcache::Entry>,8> entries;
 for(int i=0;i<8;i++){
  Stop stops[]={{0,{.2f+i*.05f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f-i*.05f}}};
  entries[i]=lutcache::global().get(lutcache::makeKey(Space::SRGB,stops,3));
  entries[i]->getPacked8();
 }
 geometrycache::Cache cache;
 for(auto shape:{Shape::Angle,Shape::Radial}){
  s.shape=shape;
  for(int mode=0;mode<3;mode++){
   s.loop=mode==1;s.reflect=mode==2;s.invert=mode==2;s.offsetPx=mode*13.7f;
   auto base=[&](int i){auto e=entries[i%8];return parallel::render(src,baseline,Depth::U8,s,e->preparedRamp(),e->getPacked8(),nullptr,cfg);};
   auto fast=[&](int i){return geometrycache::render(cache,src,dst,Depth::U8,s,entries[i%8]->getPacked8(),cfg);};
   if(fast(0)||fast(1))return 2;
   auto t0=std::chrono::steady_clock::now();
   if(!fast(2))return 3;
   double cold=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
   if(!base(2)||ref!=out)return 4;
   for(int i=0;i<8;i++)if(!base(i)||!fast(i)||ref!=out){std::cerr<<"parity failure "<<int(shape)<<" "<<mode<<" "<<i<<"\n";return 5;}
   std::array<double,21> a{},b{};
   for(int j=0;j<21;j++){
    auto run=[&](bool cached){auto t=std::chrono::steady_clock::now();bool ok=cached?fast(j):base(j);if(!ok)std::exit(6);return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();};
    if(j%2){a[j]=run(false);b[j]=run(true);}else{b[j]=run(true);a[j]=run(false);}
   }
   std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
   std::cout<<"shape="<<int(shape)<<" mode="<<mode<<" cold="<<cold<<" baseline="<<a[10]<<" cached="<<b[10]<<"ms\n";
  }
 }
 // Concurrent MFR consumers use different LUTs, identical coordinates.
 std::atomic<bool> ok{true};
 auto worker=[&](int i){
  auto e=entries[i];std::vector<uint8_t> local(out.size(),0xa5),expected(out.size(),0xa5);
  WritableBuffer l{local.data(),w,h,pitch,7,11},r{expected.data(),w,h,pitch,7,11};
  for(int j=0;j<3;j++){
   if(!geometrycache::render(cache,src,l,Depth::U8,s,e->getPacked8(),cfg)&&
      !parallel::render(src,l,Depth::U8,s,e->preparedRamp(),e->getPacked8(),nullptr,cfg)){ok=false;return;}
   if(!render(src,r,Depth::U8,s,e->preparedRamp(),e->getPacked8(),nullptr,cfg)||local!=expected){ok=false;return;}
  }
 };
 std::thread a(worker,1),b(worker,3);worker(5);a.join();b.join();if(!ok)return 7;
 // Origin and geometric changes must invalidate the stored index field.
 s.startX+=.5f;
 if(geometrycache::render(cache,src,dst,Depth::U8,s,entries[0]->getPacked8(),cfg))return 8;
 if(geometrycache::render(cache,src,dst,Depth::U8,s,entries[0]->getPacked8(),cfg))return 9;
 if(!geometrycache::render(cache,src,dst,Depth::U8,s,entries[0]->getPacked8(),cfg)||
    !parallel::render(src,baseline,Depth::U8,s,entries[0]->preparedRamp(),entries[0]->getPacked8(),nullptr,cfg)||out!=ref)return 10;
 Buffer cropped=src;cropped.width--;
 if(geometrycache::render(cache,cropped,dst,Depth::U8,s,entries[0]->getPacked8(),cfg))return 11;
 if(geometrycache::render(cache,src,dst,Depth::U16,s,entries[0]->getPacked8(),cfg))return 12;
 std::cout<<"PASS changing color and alpha stops; exact full output incl padding; concurrent MFR and invalidation\n";
}
