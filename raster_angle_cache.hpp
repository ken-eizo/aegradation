#pragma once
// Immutable integer Angle plane. Cache only repeated, source-independent
// geometry and colors, never host worlds or source-derived pixels.
#include "lut_cache.hpp"
#include "raster_parallel.hpp"
namespace gradation::raster::anglecache {
#if defined(__AVX2__)
struct Key {
 std::shared_ptr<lutcache::Entry> lut;
 std::array<uint32_t,6> geom{};
 int w=0,h=0,x=0,y=0,flags=0;
 unsigned fp=0;
 Depth depth=Depth::U8;
 bool operator==(const Key& k)const noexcept{
  return lut==k.lut&&geom==k.geom&&w==k.w&&h==k.h&&x==k.x&&y==k.y&&
         flags==k.flags&&fp==k.fp&&depth==k.depth;
 }
};
inline Key makeKey(const std::shared_ptr<lutcache::Entry>& lut,const WritableBuffer& dst,
                   const Settings& s,Depth depth){
 return {lut,{lutcache::bit(s.startX),lutcache::bit(s.startY),lutcache::bit(s.endX),
              lutcache::bit(s.endY),lutcache::bit(s.offsetPx),lutcache::bit(s.scalePercent)},
         dst.width,dst.height,dst.originX,dst.originY,
         (s.loop?1:0)|(s.reflect?2:0)|(s.invert?4:0),_mm_getcsr()&0xffc0u,depth};
}
struct Plane {Key key;std::vector<uint8_t> data;};
class Cache {
 std::mutex mutex_;
 Key pending_{};
 unsigned repetitions_=0;
 std::shared_ptr<const Plane> plane_;
public:
 static constexpr size_t limit=8u*1024u*1024u;
 std::shared_ptr<const Plane> get(const Key& key,const Settings& s,const Ramp& ramp,
                                 const PackedRamp8* p8,const PackedRamp16* p16){
  std::unique_lock lock(mutex_,std::try_to_lock);
  if(!lock)return {};
  if(plane_&&plane_->key==key)return plane_;
  if(!(pending_==key)){pending_=key;repetitions_=1;return {};}
  if(repetitions_<3&&++repetitions_<3)return {};
  if(plane_&&plane_.use_count()!=1)return {};
  plane_.reset();
  try{
   auto next=std::make_shared<Plane>();next->key=key;
   constexpr int bpp=4;
   const size_t bytes=size_t(key.w)*key.h*bpp;
   if(bytes>limit)return {};
   next->data.resize(bytes);
   WritableBuffer dst{next->data.data(),key.w,key.h,key.w*bpp,key.x,key.y};
   // Covered path uses sqrt rather than hypot; preserve its exact rounding.
   const float dx=s.endX-s.startX,dy=s.endY-s.startY;
   const float invLen=1.f/std::max(std::sqrt(dx*dx+dy*dy),1.e-6f);
   const float invScale=100.f/std::max(s.scalePercent,.0001f);
   const float offset=s.offsetPx*invLen,angle=fastAtan2(dy,dx);
   auto row=[&](int i,int n)noexcept{
    for(int y=i*key.h/n;y<(i+1)*key.h/n;y++){
     auto* out=dst.data+size_t(y)*dst.rowBytes;
     // Use the covered path's original Angle coordinate variant (not
     // the opaque generator's specialized vatan2Row). No precision drift.
     for(int x=0;x<dst.width;x+=8){
      const auto t=simd::vtransform(simd::coordinate8<Shape::Angle>(x,y,dst,s,invLen,dx,dy,angle),
                                    s,invScale,offset);
      const auto idx=_mm256_min_epi32(_mm256_set1_epi32(4096),_mm256_max_epi32(_mm256_cvtps_epi32(_mm256_mul_ps(t,_mm256_set1_ps(4096.f))),_mm256_setzero_si256()));
      const auto c=_mm256_i32gather_epi32((const int*)p8->data(),idx,4);
      _mm256_storeu_si256((__m256i*)(out+size_t(x)*4),c);
     }
    }
   };
   int n=parallel::configuredWorkers();
   if(n>1&&parallel::mayAddWorkers()&&!parallel::active.test_and_set(std::memory_order_acquire)){
    struct Release{~Release(){parallel::active.clear(std::memory_order_release);}} release;
    auto job=[&](int i)noexcept{row(i,n);};parallel::runStripes(n,job);
   }else row(0,1);
   plane_=next;return plane_;
  }catch(...){return {};}
 }
};
inline Cache& global(){static Cache cache;return cache;}
// Streaming complete cache lines keeps the reusable plane and source
// in cache instead of allocating output lines on the hot path.
// Use the identical rounded integer operations to the existing SIMD.
inline __m256i packedProduct(__m256i c,__m256i input) noexcept {
 const auto mask=_mm256_set1_epi32(255);
 const auto alpha=_mm256_and_si256(input,mask);
 if(_mm256_movemask_epi8(_mm256_cmpeq_epi32(alpha,mask))==-1)return c;
 const auto a=_mm256_or_si256(alpha,_mm256_slli_epi32(alpha,16));
 const auto m=_mm256_set1_epi32(0x00ff00ff);
 const auto lo=_mm256_mullo_epi16(_mm256_and_si256(c,m),a);
 const auto hi=_mm256_mullo_epi16(_mm256_and_si256(_mm256_srli_epi32(c,8),m),a);
 auto div=[](__m256i z){
  z=_mm256_add_epi16(z,_mm256_set1_epi16(128));
  z=_mm256_add_epi16(z,_mm256_srli_epi16(z,8));
  return _mm256_and_si256(_mm256_srli_epi16(z,8),_mm256_set1_epi32(0x00ff00ff));
 };
 return _mm256_or_si256(div(lo),_mm256_slli_epi32(div(hi),8));
}
inline void streamRow(uint8_t* out,const uint8_t* in,const uint8_t* base,int w)noexcept{
 int x=0;
 auto scalar=[&](int i){
  const unsigned a=in[4*i];
  for(int k=0;k<4;k++)out[4*i+k]=uint8_t((unsigned(base[4*i+k])*a+127)/255);
 };
 if((reinterpret_cast<uintptr_t>(out)&3u)==0){
  for(;x<w&&(reinterpret_cast<uintptr_t>(out+4*x)&63u);x++)scalar(x);
  for(;x+16<=w;x+=16){
   for(int j:{x,x+8}){
    auto v=packedProduct(_mm256_loadu_si256((const __m256i*)(base+4*j)),
                         _mm256_loadu_si256((const __m256i*)(in+4*j)));
    _mm256_stream_si256((__m256i*)(out+4*j),v);
   }
  }
 }
 for(;x+8<=w;x+=8)simd::applyPacked8(out+4*x,in+4*x,_mm256_loadu_si256((const __m256i*)(base+4*x)));
 for(;x<w;x++)scalar(x);
}

inline bool render(Cache& cache,const std::shared_ptr<lutcache::Entry>& lut,
 const Buffer& src,const WritableBuffer& dst,Depth depth,const Settings& s,
 const Ramp& ramp,const PackedRamp8* p8,const PackedRamp16* p16,blend::Config cfg){
 static const bool enabled=[](){const char* v=std::getenv("GRADATION_DISABLE_ANGLE_CACHE");return !(v&&v[0]=='1');}();
 if(!enabled||s.shape!=Shape::Angle||depth!=Depth::U8||!cfg.legacyFast()||
    !lut||!src.data||!dst.data||src.data==dst.data||
    dst.width<512||dst.height<384||dst.width%8!=0||
    std::abs(int64_t(dst.originX))>10000000||std::abs(int64_t(dst.originY))>10000000||
    !p8)return false;
 constexpr int bpp=4;
 if(!validWorld(src,bpp)||!validWorld(dst,bpp))return false;
 if(uint64_t(dst.width)*uint64_t(dst.height)*bpp>Cache::limit)return false;
 if(!std::isfinite(s.startX)||!std::isfinite(s.startY)||!std::isfinite(s.endX)||
    !std::isfinite(s.endY)||!std::isfinite(s.offsetPx)||!std::isfinite(s.scalePercent)||
    std::abs(s.startX)>1.e7f||std::abs(s.startY)>1.e7f||
    std::abs(s.endX)>1.e7f||std::abs(s.endY)>1.e7f)return false;
 const int64_t sx=int64_t(dst.originX)-src.originX,sy=int64_t(dst.originY)-src.originY;
 if(sx<0||sy<0||sx+dst.width>src.width||sy+dst.height>src.height)return false;
 auto plane=cache.get(makeKey(lut,dst,s,depth),s,ramp,p8,p16);
 if(!plane)return false;
 static const bool streamEnabled=[](){const char* v=std::getenv("GRADATION_ANGLE_CACHE_STREAM");return !(v&&v[0]=='0');}();
 const bool streaming=streamEnabled&&uint64_t(dst.width)*dst.height*4>=4u*1024u*1024u&&dst.rowBytes>0;
 auto rows=[&](int i,int n)noexcept{
  for(int y=i*dst.height/n;y<(i+1)*dst.height/n;y++){
   auto* out=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
   const auto* in=src.data+(std::ptrdiff_t)(y+sy)*src.rowBytes+sx*bpp;
   const auto* base=plane->data.data()+size_t(y)*dst.width*bpp;
   if(streaming)streamRow(out,in,base,dst.width);
   else for(int x=0;x<dst.width;x+=8)
    simd::applyPacked8(out+x*4,in+x*4,_mm256_loadu_si256((const __m256i*)(base+x*4)));
  }
  if(streaming)_mm_sfence();
 };
 const int n=std::min(4,parallel::configuredWorkers());
 if(n>1&&parallel::mayAddWorkers()&&!parallel::active.test_and_set(std::memory_order_acquire)){
  struct Release{~Release(){parallel::active.clear(std::memory_order_release);}} release;
  auto job=[&](int i)noexcept{rows(i,n);};parallel::runStripes(n,job);
 }else rows(0,1);
 return true;
}
#endif
}
