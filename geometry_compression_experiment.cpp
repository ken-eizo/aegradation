#include "geometry_compression_experiment.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int w=1920,h=1080,pitch=w*4;
 std::vector<uint8_t> in(size_t(pitch)*h),out(in.size()),reference(in.size());
 uint32_t rng=123;
 for(size_t i=0;i<in.size();i+=4){rng=rng*1664525u+1013904223u;in[i]=uint8_t(rng>>24);}
 Buffer src{in.data(),w,h,pitch,0,0};
 WritableBuffer dst{out.data(),w,h,pitch,0,0},ref{reference.data(),w,h,pitch,0,0};
 Settings s{};s.startX=960.5f;s.startY=540.5f;s.endX=1450.3f;s.endY=800.9f;
 const Stop stops[]={{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}};
 auto entry=lutcache::global().get(lutcache::makeKey(Space::SRGB,stops,3));
 auto* lut=entry->getPacked8();
 blend::Config cfg{blend::Mode::Replace,blend::Mode::Multiply,0};
 geometrycache::Cache cache;
 for(auto shape:{Shape::Angle,Shape::Radial}){
  s.shape=shape;
  for(int k=0;k<3;k++){
   s.loop=k==1;s.reflect=k==2;s.invert=k==2;
   auto key=anglecache::makeKey({},dst,s,Depth::U8);
   cache.get(key,s);cache.get(key,s);auto plane=cache.get(key,s);
   if(!plane)return 1;
   auto start=std::chrono::steady_clock::now();
   auto field=geometrycompression::encode(*plane);
   double encodeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   if(!geometrycompression::verify(field,*plane))return 2;
   if(!geometrycache::render(cache,src,ref,Depth::U8,s,lut,cfg))return 3;
   geometrycompression::render(field,src,dst,*lut);
   if(out!=reference)return 4;
   std::array<double,15>a{},b{};
   for(int i=0;i<15;i++){
    auto run=[&](bool compressed){auto t=std::chrono::steady_clock::now();
     if(compressed)geometrycompression::render(field,src,dst,*lut);
     else if(!geometrycache::render(cache,src,ref,Depth::U8,s,lut,cfg))std::exit(5);
     return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();};
    if(i%2){a[i]=run(false);b[i]=run(true);}else{b[i]=run(true);a[i]=run(false);}
   }
   std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
   std::cout<<"shape="<<int(shape)<<" mode="<<k<<" bytes="<<field.bytes()
            <<" ratio="<<double(field.bytes())/(w*h*2)<<" encode="<<encodeMs
            <<" raw="<<a[7]<<" compressed="<<b[7]<<"ms\n";
  }
 }
 std::cout<<"PASS compact geometry lossless decode and exact compositing\n";
}
