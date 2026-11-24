#pragma once
// Compact geometry field: exactly the covered U8 kernel's quantized LUT
// index, independent of stops, color space, source and current frame.
#include "raster_angle_cache.hpp"
namespace gradation::raster::geometrycache {
#if defined(__AVX2__)
using Key=anglecache::Key;
struct Plane {Key key;std::vector<uint16_t> index;};
class Cache {
 std::mutex mutex_;
 Key pending_{};
 unsigned repeats_=0;
 std::shared_ptr<const Plane> plane_;
public:
 static constexpr size_t limit=4u*1024u*1024u;
 std::shared_ptr<const Plane> get(Key key,const Settings& s){
  key.lut.reset(); // geometry is independent of the stop/color cache
  // shape encoded separately from the transform flags
  key.flags|=int(s.shape)<<3;
  std::unique_lock lock(mutex_,std::try_to_lock);
  if(!lock)return {};
  if(plane_&&plane_->key==key)return plane_;
  if(!(pending_==key)){pending_=key;repeats_=1;return {};}
  if(repeats_<3&&++repeats_<3)return {};
  if(plane_&&plane_.use_count()!=1)return {};
  plane_.reset();
  if(uint64_t(key.w)*key.h*2>limit)return {};
  try{
   auto next=std::make_shared<Plane>();next->key=key;
   next->index.resize(size_t(key.w)*key.h);
   WritableBuffer coords{nullptr,key.w,key.h,key.w*4,key.x,key.y};
   const float dx=s.endX-s.startX,dy=s.endY-s.startY;
   const float invLen=1.f/std::max(std::sqrt(dx*dx+dy*dy),1.e-6f);
   const float invScale=100.f/std::max(s.scalePercent,.0001f);
   const float offset=s.offsetPx*invLen,angle=fastAtan2(dy,dx);
   auto rows=[&](int i,int n)noexcept{
    for(int y=i*key.h/n;y<(i+1)*key.h/n;y++){
     auto* out=next->index.data()+size_t(y)*key.w;
     for(int x=0;x<key.w;x+=8){
      const auto c=s.shape==Shape::Angle?
       simd::coordinate8<Shape::Angle>(x,y,coords,s,invLen,dx,dy,angle):
       simd::coordinate8<Shape::Radial>(x,y,coords,s,invLen,dx,dy,angle);
      const auto t=simd::vtransform(c,s,invScale,offset);
      const auto idx=_mm256_min_epi32(_mm256_set1_epi32(4096),_mm256_max_epi32(
        _mm256_cvtps_epi32(_mm256_mul_ps(t,_mm256_set1_ps(4096.f))),_mm256_setzero_si256()));
      _mm_storeu_si128((__m128i*)(out+x),_mm_packus_epi32(_mm256_castsi256_si128(idx),_mm256_extracti128_si256(idx,1)));
     }
    }
   };
   const int n=parallel::configuredWorkers();
   if(n>1&&parallel::mayAddWorkers()&&!parallel::active.test_and_set(std::memory_order_acquire)){
    struct Release{~Release(){parallel::active.clear(std::memory_order_release);}} release;
    auto job=[&](int i)noexcept{rows(i,n);};parallel::runStripes(n,job);
   }else rows(0,1);
   plane_=next;return plane_;
  }catch(...){return {};}
 }
};
inline Cache& global(){static Cache c;return c;}
inline bool render(Cache& cache,const Buffer& src,const WritableBuffer& dst,
 Depth depth,const Settings& s,const PackedRamp8* p8,blend::Config cfg){
 static const bool enabled=[](){const char* v=std::getenv("GRADATION_DISABLE_GEOMETRY_CACHE");return !(v&&v[0]=='1');}();
 if(!enabled||depth!=Depth::U8||!cfg.legacyFast()||!p8||!src.data||!dst.data||
    src.data==dst.data||(s.shape!=Shape::Angle&&s.shape!=Shape::Radial)||
    dst.width<512||dst.height<384||dst.width%8||
    !validWorld(src,4)||!validWorld(dst,4)||
    uint64_t(dst.width)*dst.height*2>Cache::limit)return false;
 for(float v:{s.startX,s.startY,s.endX,s.endY,s.offsetPx,s.scalePercent})
  if(!std::isfinite(v)||std::abs(v)>1.e7f)return false;
 if(std::abs(int64_t(dst.originX))>10000000||std::abs(int64_t(dst.originY))>10000000)return false;
 const int64_t sx=int64_t(dst.originX)-src.originX,sy=int64_t(dst.originY)-src.originY;
 if(sx<0||sy<0||sx+dst.width>src.width||sy+dst.height>src.height)return false;
 auto plane=cache.get(anglecache::makeKey({},dst,s,depth),s);
 if(!plane)return false;
 const int* lut=(const int*)p8->data();
 const bool streaming=dst.rowBytes>0&&uint64_t(dst.width)*dst.height*4>=4u*1024u*1024u;
 auto rows=[&](int i,int n)noexcept{
  for(int y=i*dst.height/n;y<(i+1)*dst.height/n;y++){
   auto* out=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
   const auto* in=src.data+(std::ptrdiff_t)(y+sy)*src.rowBytes+sx*4;
   const auto* ix=plane->index.data()+size_t(y)*dst.width;
   const auto sample=[&](int x){
    const auto idx=_mm256_cvtepu16_epi32(_mm_loadu_si128((const __m128i*)(ix+x)));
    return _mm256_i32gather_epi32(lut,idx,4);
   };
   int x=0;
   if(streaming&&(reinterpret_cast<uintptr_t>(out)&3u)==0){
    for(;x<dst.width&&(reinterpret_cast<uintptr_t>(out+x*4)&63u);x++){
     const uint32_t c=(*p8)[ix[x]];
     const unsigned a=in[x*4];
     for(int k=0;k<4;k++)out[x*4+k]=uint8_t((((c>>(k*8))&255u)*a+127)/255);
    }
    for(;x+16<=dst.width;x+=16){
     for(int j:{x,x+8}){
      const auto v=anglecache::packedProduct(sample(j),_mm256_loadu_si256((const __m256i*)(in+j*4)));
      _mm256_stream_si256((__m256i*)(out+j*4),v);
     }
    }
   }
   for(;x+8<=dst.width;x+=8)simd::applyPacked8(out+x*4,in+x*4,sample(x));
   for(;x<dst.width;x++){
    const uint32_t c=(*p8)[ix[x]];
    const unsigned a=in[x*4];
    for(int k=0;k<4;k++)out[x*4+k]=uint8_t((((c>>(k*8))&255u)*a+127)/255);
   }
  }
  if(streaming)_mm_sfence();
 };
 const int n=parallel::configuredWorkers();
 if(n>1&&parallel::mayAddWorkers()&&!parallel::active.test_and_set(std::memory_order_acquire)){
  struct Release{~Release(){parallel::active.clear(std::memory_order_release);}} release;
  auto job=[&](int i)noexcept{rows(i,n);};parallel::runStripes(n,job);
 }else rows(0,1);
 return true;
}
#endif
}
