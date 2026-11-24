#pragma once
// CPU rasterization core for Adobe-compatible ARGB pixels.
// Standalone and host independent: all data passed by value or const reference.
#include "../../core/gradient_core.hpp"
#include "blend_modes.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <type_traits>
#include <vector>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
namespace gradation::raster {
struct Pixel8 {uint8_t a,r,g,b;};
struct Pixel16 {uint16_t a,r,g,b;};
struct Pixel32 {float a,r,g,b;};
static_assert(sizeof(Pixel8)==4&&sizeof(Pixel16)==8&&sizeof(Pixel32)==16);
struct Buffer {
 const uint8_t* data=nullptr;
 int width=0,height=0,rowBytes=0,originX=0,originY=0;
};
struct WritableBuffer {
 uint8_t* data=nullptr;
 int width=0,height=0,rowBytes=0,originX=0,originY=0;
};
using Ramp=std::array<Color,4097>;
using PackedRamp8=std::array<uint32_t,4097>;
using PackedRamp16=std::array<Pixel16,4097>;
// A direct 32bpc fast path is valid ONLY for two stops at [0,1] in sRGB.
// This remains a hint, not a replacement for the high-quality LUT.
struct DirectRamp {
 Color first{},last{};
 // Optional 3-stop sRGB segmented linear interpolation. The two existing
 // aggregate fields remain unchanged for older standalone callers.
 Color middle{};
 float middlePosition=.5f;
 int count=2;
};
// Exact float scalar endpoint/tail evaluator matching the SIMD piecewise
// affine fast path. Required for odd tile widths when no LUT was generated.
inline Color sampleDirectSrgb(const DirectRamp& d,float t) noexcept {
 const float u=std::isfinite(t)?clamp(t):0.f;
 const Color *a=&d.first,*b=&d.last;
 float v=u;
 if(d.count==3){
  if(u<d.middlePosition){
   a=&d.first;b=&d.middle;v=u/d.middlePosition;
  }else{
   a=&d.middle;b=&d.last;
   v=(u-d.middlePosition)/(1.f-d.middlePosition);
  }
 }
 return {a->r+(b->r-a->r)*v,a->g+(b->g-a->g)*v,
         a->b+(b->b-a->b)*v,a->a+(b->a-a->a)*v};
}
inline uint8_t u8(float c) noexcept {
 return (uint8_t)(clamp(c)*255.f+.5f);
}
inline PackedRamp8 makePacked8(const Ramp& ramp) noexcept {
 PackedRamp8 out{};
 for(int i=0;i<=4096;i++){
  const Color& c=ramp[i];
  const float a=clamp(c.a);
  const uint32_t word=(uint32_t)u8(a)|((uint32_t)u8(c.r*a)<<8)
                      |((uint32_t)u8(c.g*a)<<16)|((uint32_t)u8(c.b*a)<<24);
  out[i]=word;
 }
 return out;
}
inline PackedRamp16 makePacked16(const Ramp& ramp) noexcept {
 PackedRamp16 out{};
 for(int i=0;i<=4096;i++){
  const Color& c=ramp[i];const float a=clamp(c.a);
  out[i]={(uint16_t)(a*32768.f+.5f),
          (uint16_t)(clamp(c.r*a)*32768.f+.5f),
          (uint16_t)(clamp(c.g*a)*32768.f+.5f),
          (uint16_t)(clamp(c.b*a)*32768.f+.5f)};
 }
 return out;
}
// Preserve high 16-bit output precision: 4097-sample nearest-neighbor access
// would produce 8-count steps across an otherwise smooth black-white ramp.
inline Pixel16 lookup16(const PackedRamp16& ramp,float t) noexcept {
 if(!std::isfinite(t))t=0.f;
 const float v=clamp(t)*4096.f;
 const int idx=std::min(4095,(int)v);
 const float f=v-(float)idx;
 const Pixel16& a=ramp[idx],&b=ramp[idx+1];
 auto blend=[&](uint16_t x,uint16_t y)->uint16_t {
  return (uint16_t)std::clamp((int)(x+((int)y-(int)x)*f+.5f),0,32768);
 };
 return {blend(a.a,b.a),blend(a.r,b.r),blend(a.g,b.g),blend(a.b,b.b)};
}
inline Color lookup(const Ramp& ramp,float t) noexcept{
 if(!std::isfinite(t))t=0.f;
 float coord=clamp(t)*4096.f;
 int i=std::min(4095,(int)coord);
 float a=coord-i;
 const Color& l=ramp[i],&r=ramp[i+1];
 return {l.r+a*(r.r-l.r),l.g+a*(r.g-l.g),
         l.b+a*(r.b-l.b),l.a+a*(r.a-l.a)};
}
inline float fastAtan(float z) noexcept {
 // Minimax degree-11 odd polynomial on [-1,1], max error ~3.4e-6 rad.
 float zz=z*z;
 return z*(.9999956488609314f+zz*(-.3329949676990509f+
  zz*(.1956379562616348f+zz*(-.1212436482310295f+
  zz*(.05748184770345688f+zz*(-.013482107780873775f))))));
}
inline float fastAtan2(float y,float x) noexcept {
 constexpr float PI=3.14159265358979323846f;
 constexpr float HALF_PI=1.57079632679489661923f;
 float ax=std::abs(x),ay=std::abs(y);
 if(ax+ay<1.e-20f)return 0.f;
 float a=(ax>=ay)?fastAtan(ay/std::max(ax,1e-20f)):
                   HALF_PI-fastAtan(ax/std::max(ay,1e-20f));
 if(x<0)a=PI-a;
 if(y<0)a=-a;
 return a;
}
inline float transform(float t,float inverseScale,float offset,
                       bool loop,bool reflect,bool invert) noexcept {
 if(!std::isfinite(t))t=0.f;
 t=t*inverseScale+offset;
 if(!std::isfinite(t))t=0.f;
 if(reflect){t=t-2.f*std::floor(t*.5f);t=1.f-std::abs(1.f-t);}
 else if(loop){t=t-std::floor(t);}
 else t=clamp(t);
 return invert?1.f-t:t;
}
template<class Pixel> struct Ops;
template<>struct Ops<Pixel8>{
 static float alpha(const Pixel8& x) noexcept{return x.a/255.f;}
 static float luma(const Pixel8& x) noexcept{
  if(x.a==0)return 0;
  // Accurate linear-light weighted Rec. 709 coefficients of encoded input.
  return clamp((.2126f*x.r+.7152f*x.g+.0722f*x.b)/x.a);
 }
 static Pixel8 output(Color c,Pixel8 src) noexcept {
  const float alpha=clamp(c.a)*src.a/255.f;
  return {u8(alpha),u8(c.r*alpha),u8(c.g*alpha),u8(c.b*alpha)};
 }
};
template<>struct Ops<Pixel16>{
 static float alpha(const Pixel16& x) noexcept{return x.a/32768.f;}
 static float luma(const Pixel16& x) noexcept {
  if(x.a==0)return 0;
  return clamp((.2126f*x.r+.7152f*x.g+.0722f*x.b)/x.a);
 }
 static Pixel16 output(Color c,Pixel16 src) noexcept {
  const float a=clamp(c.a)*clamp(alpha(src));
  return {(uint16_t)(a*32768.f+.5f),(uint16_t)(clamp(c.r*a)*32768.f+.5f),
   (uint16_t)(clamp(c.g*a)*32768.f+.5f),(uint16_t)(clamp(c.b*a)*32768.f+.5f)};
 }
};
template<>struct Ops<Pixel32>{
 static float alpha(const Pixel32& x) noexcept{return x.a;}
 static float luma(const Pixel32& x) noexcept {
  if(!std::isfinite(x.a)||x.a<=1e-10f||
     !std::isfinite(x.r)||!std::isfinite(x.g)||!std::isfinite(x.b))return 0.f;
  return clamp((.2126f*x.r+.7152f*x.g+.0722f*x.b)/x.a);
 }
 static Pixel32 output(Color c,Pixel32 src) noexcept {
  float a=clamp(std::isfinite(c.a)?c.a:0.f)*clamp(std::isfinite(src.a)?src.a:0.f);
  return {a,c.r*a,c.g*a,c.b*a};
 }
};
inline bool allOpaque8(const uint8_t* rgba,int n) noexcept {
#if defined(__AVX2__)
 const __m256i mask=_mm256_set1_epi32(0xff);
 const __m256i full=_mm256_set1_epi32(0xff);
 int i=0;
 for(;i+8<=n;i+=8) {
  const __m256i v=_mm256_loadu_si256((const __m256i*)(rgba+(size_t)i*4));
  const __m256i cmp=_mm256_cmpeq_epi32(_mm256_and_si256(v,mask),full);
  if(_mm256_movemask_epi8(cmp)!=-1)return false;
 }
 for(;i<n;i++)if(rgba[(size_t)i*4]!=255)return false;
#else
 for(int i=0;i<n;i++)if(rgba[(size_t)i*4]!=255)return false;
#endif
 return true;
}
inline bool allOpaque16(const uint8_t* argb,int n) noexcept {
#if defined(__AVX2__)
 const __m256i mask=_mm256_set1_epi64x(0xffff);
 const __m256i value=_mm256_set1_epi64x(32768);
 int i=0;
 for(;i+4<=n;i+=4){
  const __m256i v=_mm256_loadu_si256((const __m256i*)(argb+(size_t)i*8));
  const __m256i cmp=_mm256_cmpeq_epi64(_mm256_and_si256(v,mask),value);
  if(_mm256_movemask_epi8(cmp)!=-1)return false;
 }
 for(;i<n;i++){uint16_t a;std::memcpy(&a,argb+(size_t)i*8,2);if(a!=32768)return false;}
#else
 for(int i=0;i<n;i++){uint16_t a;std::memcpy(&a,argb+(size_t)i*8,2);if(a!=32768)return false;}
#endif
 return true;
}
inline bool allOpaque32(const uint8_t* argb,int n) noexcept {
#if defined(__AVX2__)
 const __m256i mask=_mm256_setr_epi32(-1,0,0,0,-1,0,0,0);
 const __m256i value=_mm256_setr_epi32(0x3f800000,0,0,0,0x3f800000,0,0,0);
 int i=0;
 for(;i+2<=n;i+=2){
  const __m256i v=_mm256_loadu_si256((const __m256i*)(argb+(size_t)i*16));
  const __m256i cmp=_mm256_cmpeq_epi32(_mm256_and_si256(v,mask),value);
  if(_mm256_movemask_epi8(cmp)!=-1)return false;
 }
 for(;i<n;i++){float a;std::memcpy(&a,argb+(size_t)i*16,4);if(a!=1.f)return false;}
#else
 for(int i=0;i<n;i++){float a;std::memcpy(&a,argb+(size_t)i*16,4);if(a!=1.f)return false;}
#endif
 return true;
}
#include "raster_avx2.hpp"
// The SIMD batch compositor calls the same scalar tail channel encoders.
template<class P>inline Color loadSource(const P& p)noexcept;
template<class P>inline P storeComposite(Color c)noexcept;
#include "blend_avx2.hpp"
#include "blend_raster.hpp"
#if defined(__AVX2__)
// Fill a constant ARGB row using an AVX2 broadcast pattern: no per-pixel
// LUT lookups, allocation, branching, FP conversion, or GDI/AE calls.
template<class P>inline void fillFlatRow(uint8_t* row,int n,const P& px)noexcept {
 alignas(32)uint8_t pattern[32];
 for(int i=0;i<32/(int)sizeof(P);i++)
  std::memcpy(pattern+(size_t)i*sizeof(P),&px,sizeof(P));
 const __m256i block=_mm256_load_si256((const __m256i*)pattern);
 const int step=32/(int)sizeof(P);
 int i=0;
 for(;i+step<=n;i+=step)
  _mm256_storeu_si256((__m256i*)(row+(size_t)i*sizeof(P)),block);
 for(;i<n;i++)
  std::memcpy(row+(size_t)i*sizeof(P),&px,sizeof(P));
}
#endif
#if defined(__AVX2__)
// Constant-row NT stores with exact scalar prefix/suffix, never writing
// padding or partial cache lines. No read-for-ownership on bulk writes.
template<class P> inline void streamFlatRow(uint8_t* row,int n,const P& px) noexcept {
 if((reinterpret_cast<uintptr_t>(row)%sizeof(P))!=0){fillFlatRow(row,n,px);return;}
 alignas(32) P pattern[32/sizeof(P)];
 for(auto& q:pattern)q=px;
 const auto v=_mm256_load_si256((const __m256i*)pattern);
 constexpr int step=64/sizeof(P);
 int x=0;
 for(;x<n&&(reinterpret_cast<uintptr_t>(row+(size_t)x*sizeof(P))&63);x++)
  std::memcpy(row+(size_t)x*sizeof(P),&px,sizeof(P));
 for(;x+step<=n;x+=step){
  auto* ptr=row+(size_t)x*sizeof(P);
  _mm256_stream_si256((__m256i*)ptr,v);
  _mm256_stream_si256((__m256i*)(ptr+32),v);
 }
 for(;x<n;x++)std::memcpy(row+(size_t)x*sizeof(P),&px,sizeof(P));
}
#endif
// Copy a single reusable row directly to memory, avoiding cache-line
// write allocation for large outputs. Cached scalar edges never overlap
// cache lines touched by non-temporal stores. Caller fences before return.
#if defined(__AVX2__)
inline void streamCopyRow(uint8_t* dst,const uint8_t* src,size_t n) noexcept {
 const size_t head=std::min(n,size_t((64-(reinterpret_cast<uintptr_t>(dst)&63))&63));
 if(head)std::memcpy(dst,src,head);
 size_t i=head;
 for(;i+64<=n;i+=64){
  const __m256i a=_mm256_loadu_si256((const __m256i*)(src+i));
  const __m256i b=_mm256_loadu_si256((const __m256i*)(src+i+32));
  _mm256_stream_si256((__m256i*)(dst+i),a);
  _mm256_stream_si256((__m256i*)(dst+i+32),b);
 }
 if(i<n)std::memcpy(dst+i,src+i,n-i);
}
#endif
template<class P,Shape Mode>
void renderImpl(const Buffer& src,const WritableBuffer& dst,const Settings& s,
                const Ramp& ramp,const PackedRamp8* packed, const PackedRamp16* packed16,
                const blend::Config& blending,
                const DirectRamp* direct=nullptr) noexcept {
 if(!dst.data||dst.width<=0||dst.height<=0||src.width<0||src.height<0||dst.rowBytes==0)return;
#if defined(__AVX2__)
 if constexpr(Mode!=Shape::Mapping){
  // No input pixel can influence Replace Color + Replace Alpha.
  // A pure-gradient kernel eliminates all source reads and per-pixel RGB
  // compositing. Transform / LUT quality remains depth-aware.
  if(blending.color==blend::Mode::Replace &&
     blending.alpha==blend::Mode::Replace){
   const float dx=s.endX-s.startX,dy=s.endY-s.startY;
   const float length=std::max(std::hypot(dx,dy),1.e-6f);
   const float invLen=1.f/length;
   const float invScale=100.f/std::max(s.scalePercent,.0001f);
   const float offset=s.offsetPx*invLen;
   const float angle=fastAtan2(dy,dx);
   // Exact horizontal Linear gradient: all output rows are identical.
   // Compute ONE row, then stream it to every remaining row.
   // Exact dy==0 check avoids approximating shallow, animated slopes.
   if constexpr(Mode==Shape::Linear){
    if(dy==0.f){
     simd::opaqueRow<P,Mode>(dst,0,s,ramp,packed,packed16,
                            invLen,dx,dy,angle,invScale,offset,direct);
     const size_t rowSize=(size_t)dst.width*sizeof(P);
      const bool stream=(uint64_t)rowSize*dst.height>=4ull*1024*1024&&
                        dst.rowBytes>0;
      for(int y=1;y<dst.height;y++){
       auto* row=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
       if(stream)streamCopyRow(row,dst.data,rowSize);
       else std::memcpy(row,dst.data,rowSize);
      }
      if(stream)_mm_sfence();
     return;
    }
   }
   // Exact vertical gradient. Every pixel of one scanline shares t.
   // Restrict to complete SIMD blocks and finite ordinary coordinates.
   if constexpr(Mode==Shape::Linear){
    #if !defined(GRADATION_DISABLE_VERTICAL_REUSE)
    if(dx==0.f&&dst.width>=128&&dst.width%8==0&&
       std::isfinite(s.startX)&&std::abs(s.startX)<1.e8f){
     alignas(32) P sample[8];
     WritableBuffer one{reinterpret_cast<uint8_t*>(sample),8,1,
       (int)sizeof(sample),dst.originX,dst.originY};
     const bool stream=sizeof(P)>=8&&
       (uint64_t)dst.width*dst.height*sizeof(P)>=4ull*1024*1024&&dst.rowBytes>0;
     for(int y=0;y<dst.height;y++){
      simd::opaqueRow<P,Mode>(one,0,s,ramp,packed,packed16,
                             invLen,dx,dy,angle,invScale,offset,direct);
      auto* row=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
      if(stream)streamFlatRow(row,dst.width,sample[0]);
      else fillFlatRow(row,dst.width,sample[0]);
      ++one.originY;
     }
     if(stream)_mm_sfence();
     return;
    }
#endif
   }
   // Radial output is exactly symmetric about its Y center. If the
   // center falls on a pixel/half-pixel grid, the second row in every
   // mirrored pair is identical to an already-computed output row.
   // Reuse the output row itself; no scratch buffer or frame allocation.
   const float centerInTile=s.startY-(float)dst.originY;
   const float mirrorDouble=2.f*centerInTile;
   const bool canMirror=Mode==Shape::Radial &&
     std::isfinite(mirrorDouble)&&
     std::abs(mirrorDouble)<10000000.f&&
     mirrorDouble==std::trunc(mirrorDouble);
   // Non-looping coordinates beyond both ends of the gradient have exactly
   // constant output. Invalidate this optimization on Loop/Reflect, extreme
   // inputs, or anything near the interpolated color boundary.
   int previousFlat=-1;
   for(int y=0;y<dst.height;y++){
    int flat=-1;
#ifndef GRADATION_DISABLE_FLAT_ROWS
    if constexpr(Mode==Shape::Linear||Mode==Shape::Radial){
     if(!s.loop&&!s.reflect){
      const float fx0=(float)dst.originX-s.startX;
      const float fx1=(float)(dst.originX+dst.width-1)-s.startX;
      const float fy=(float)(dst.originY+y)-s.startY;
      float tMin{},tMax{};
      if constexpr(Mode==Shape::Linear){
       const float inv2=invLen*invLen;
       const float t0=(fx0*dx+fy*dy)*inv2*invScale+offset;
       const float t1=(fx1*dx+fy*dy)*inv2*invScale+offset;
       tMin=std::min(t0,t1);
       tMax=std::max(t0,t1);
      }else{
       const float nearestX=std::clamp(0.f,std::min(fx0,fx1),std::max(fx0,fx1));
       const float farthestX=std::max(std::abs(fx0),std::abs(fx1));
       tMin=std::hypot(nearestX,fy)*invLen*invScale+offset;
       tMax=std::hypot(farthestX,fy)*invLen*invScale+offset;
      }
      // Guard the legacy SIMD |coordinate|<1e8 sanitization semantics.
      if(std::isfinite(tMin)&&std::isfinite(tMax)&&
         std::abs(tMin)<1e7f&&std::abs(tMax)<1e7f){
       if(tMin>1.0002f)flat=s.invert?0:4096;
       else if(tMax<-.0002f)flat=s.invert?4096:0;
      }
     }
    }
#endif
    if(flat>=0){
     uint8_t* dstRow=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
#if defined(GRADATION_FLAT_ALWAYS_FILL)
     constexpr bool copyLastRow=false;
#else
     // Byte pixels are faster to broadcast directly without an extra read.
     // U16/F32 generally benefit from the established row memcpy path.
     constexpr bool copyLastRow=!std::is_same_v<P,Pixel8>;
#endif
     if(copyLastRow&&previousFlat==flat&&y>0){
      std::memcpy(dstRow,dstRow-(std::ptrdiff_t)dst.rowBytes,
                  (size_t)dst.width*sizeof(P));
     }else{
      P pixel{};
      if constexpr(std::is_same_v<P,Pixel8>){
       const uint32_t q=(*packed)[(size_t)flat];
       std::memcpy(&pixel,&q,4);
      }else if constexpr(std::is_same_v<P,Pixel16>){
       pixel=(*packed16)[(size_t)flat];
      }else{
       pixel=Ops<Pixel32>::output(
        direct?(flat==0?direct->first:direct->last):ramp[(size_t)flat],
        Pixel32{1,0,0,0});
      }
      fillFlatRow(dstRow,dst.width,pixel);
     }
     previousFlat=flat;
     continue;
    }
    previousFlat=-1;
    if(canMirror){
     const int first=(int)mirrorDouble-y;
     if(first>=0&&first<y){
      std::memcpy(dst.data+(std::ptrdiff_t)y*dst.rowBytes,
                  dst.data+(std::ptrdiff_t)first*dst.rowBytes,
                  (size_t)dst.width*sizeof(P));
      continue;
     }
    }
    simd::opaqueRow<P,Mode>(dst,y,s,ramp,packed,packed16,
                           invLen,dx,dy,angle,invScale,offset,direct);
   }
   return;
  }
 }
#endif
#if !defined(__AVX2__)
 if constexpr(Mode==Shape::Linear){
  if(blending.color==blend::Mode::Replace&&blending.alpha==blend::Mode::Replace&&
     s.endY==s.startY&&dst.height>1){
   WritableBuffer first=dst;first.height=1;
   renderComposite<P,Mode>(Buffer{},first,s,ramp,blending,direct);
   for(int y=1;y<dst.height;y++)
    std::memcpy(dst.data+(std::ptrdiff_t)y*dst.rowBytes,dst.data,size_t(dst.width)*sizeof(P));
   return;
  }
 }
#endif
 if(!blending.legacyFast()){
  renderComposite<P,Mode>(src,dst,s,ramp,blending,direct);
  return;
 }
 if(!src.data||src.rowBytes==0){
  // Multiply alpha with an absent source must deterministically output
  // transparent black, not leave the AE output buffer uninitialized.
  for(int row=0;row<dst.height;row++)
   std::memset(dst.data+(std::ptrdiff_t)row*dst.rowBytes,0,(size_t)dst.width*sizeof(P));
  return;
 }
 float dx=s.endX-s.startX,dy=s.endY-s.startY;
 float length=std::max(std::sqrt(dx*dx+dy*dy),1e-6f);
 float invLen=1.f/length;
 float invScale=100.f/std::max(s.scalePercent,.0001f);
 float offset=s.offsetPx*invLen;
 float a=dx*invLen*invLen,b=dy*invLen*invLen;
 float startAngle=fastAtan2(dy,dx);
 constexpr float INV_TAU=1.f/6.28318530717958647692f;
 constexpr int K=4096;
 const int shiftX=dst.originX-src.originX,shiftY=dst.originY-src.originY;
 // No persistent row copies or extra allocations for translucent frames.
 // When opaque, check the input alpha only ONCE per row; linear horizontal
 // gradients then replicate a prebuilt output scanline.
#if defined(__AVX2__) && !defined(GRADATION_DISABLE_INTEGER_ROW_CACHE)
 // Exact integer horizontal gradient: sample a single packed row and
 // multiply only source alpha. Retain existing LUT quantization and
 // scalar behavior for odd widths, cropping or arbitrary transforms.
 if constexpr(Mode==Shape::Linear&&!std::is_same_v<P,Pixel32>){
  #if !defined(GRADATION_DISABLE_VERTICAL_REUSE)
  // Reuse one exact packed SIMD sample per vertical scanline for U8
  // and U16. Restrict to full 8-pixel blocks so scalar tail rounding
  // remains unchanged. The source alpha arithmetic is unchanged.
  if(dx==0.f&&dy!=0.f&&dst.width>=128&&dst.width%8==0&&
     std::isfinite(s.startX)&&std::abs(s.startX)<1.e8f&&
     shiftX>=0&&shiftY>=0&&
     (int64_t)shiftX+dst.width<=src.width&&
     (int64_t)shiftY+dst.height<=src.height){
   alignas(32) P sample[8];
   WritableBuffer one{reinterpret_cast<uint8_t*>(sample),8,1,
                      (int)sizeof(sample),dst.originX,dst.originY};
   for(int y=0;y<dst.height;y++){
    simd::opaqueRow<P,Mode>(one,0,s,ramp,packed,packed16,
                           invLen,dx,dy,startAngle,invScale,offset,direct);
    ++one.originY;
    const auto c=_mm256_load_si256((const __m256i*)sample);
    const auto* in=src.data+(std::ptrdiff_t)(y+shiftY)*src.rowBytes+(size_t)shiftX*sizeof(P);
    auto* out=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
    if constexpr(std::is_same_v<P,Pixel8>){
     for(int x=0;x<dst.width;x+=8)
      simd::applyPacked8(out+(size_t)x*4,in+(size_t)x*4,c);
    }else{
     // All four lanes hold the same premultiplied Q15 color.
     for(int x=0;x<dst.width;x+=4){
      const auto source=_mm256_loadu_si256((const __m256i*)(in+(size_t)x*8));
      _mm256_storeu_si256((__m256i*)(out+(size_t)x*8),simd::mulSourceAlpha16(c,source));
     }
    }
   }
   return;
  }
#endif
  if(dy==0.f&&dst.width>=128&&dst.width<=4096&&dst.width%8==0&&
     dst.height>=16&&shiftX>=0&&shiftY>=0&&
     (int64_t)shiftX+dst.width<=src.width&&
     (int64_t)shiftY+dst.height<=src.height){
   alignas(32) std::array<P,4096> cache;
   WritableBuffer scan{reinterpret_cast<uint8_t*>(cache.data()),dst.width,1,
                       dst.width*(int)sizeof(P),dst.originX,dst.originY};
   simd::opaqueRow<P,Mode>(scan,0,s,ramp,packed,packed16,
                            invLen,dx,dy,startAngle,invScale,offset,direct);
   for(int y=0;y<dst.height;y++){
    const auto* in=src.data+(std::ptrdiff_t)(y+shiftY)*src.rowBytes+(size_t)shiftX*sizeof(P);
    auto* out=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
    for(int x=0;x<dst.width;x+=8){
     if constexpr(std::is_same_v<P,Pixel8>){
      simd::applyPacked8(out+(size_t)x*4,in+(size_t)x*4,
        _mm256_load_si256((const __m256i*)(cache.data()+x)));
     }else{
      const auto a=_mm256_load_si256((const __m256i*)(cache.data()+x));
      const auto b=_mm256_load_si256((const __m256i*)(cache.data()+x+4));
      const auto sa=_mm256_loadu_si256((const __m256i*)(in+(size_t)x*8));
      const auto sb=_mm256_loadu_si256((const __m256i*)(in+(size_t)x*8+32));
      _mm256_storeu_si256((__m256i*)(out+(size_t)x*8),simd::mulSourceAlpha16(a,sa));
      _mm256_storeu_si256((__m256i*)(out+(size_t)x*8+32),simd::mulSourceAlpha16(b,sb));
     }
    }
   }
   return;
  }
 }
#endif
 int opaqueTemplateRow=-1;
 const bool horizontal=std::abs(dy)<1.e-7f;
 const bool fullyInBounds=shiftX>=0&&shiftX+dst.width<=src.width;
 // Mapping depends ONLY on source pixel values, not output coordinates.
 // Adjacent identical source scanlines share exactly the same output.
 // Validate source bytes every frame to keep AE's effect cache coherent.
 const uint8_t* mappedSourceLast=nullptr;
 int mappedOutputRow=-1;
 const bool allowRowReuse=src.data!=dst.data;
 for(int y=0;y<dst.height;y++){
  uint8_t* row=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
  const int srcY=y+shiftY;
  const uint8_t* inrow=(srcY>=0&&srcY<src.height)?
      src.data+(std::ptrdiff_t)srcY*src.rowBytes:nullptr;
  if constexpr(Mode==Shape::Mapping){
   if(inrow&&fullyInBounds&&allowRowReuse){
    const uint8_t* current=inrow+(size_t)shiftX*sizeof(P);
    if(mappedSourceLast&&
       std::memcmp(mappedSourceLast,current,(size_t)dst.width*sizeof(P))==0){
     std::memcpy(row,dst.data+(std::ptrdiff_t)mappedOutputRow*dst.rowBytes,
                 (size_t)dst.width*sizeof(P));
     continue;
    }
    mappedSourceLast=current;
    mappedOutputRow=y;
   }else{
    mappedSourceLast=nullptr;
    mappedOutputRow=-1;
   }
  }
#if defined(__AVX2__)
  // Smart Render may supply differently cropped source/output tiles.
  // Process the intersecting span with SIMD and write transparent black
  // outside source bounds; never downgrade the entire row to scalar.
  if(!inrow||!fullyInBounds){
   constexpr size_t px=sizeof(P);
   const int64_t lo=-(int64_t)shiftX;
   const int64_t hi=(int64_t)src.width-(int64_t)shiftX;
   const int first=(int)std::clamp<int64_t>(lo,0,dst.width);
   const int last=(int)std::clamp<int64_t>(hi,0,dst.width);
   if(!inrow||last<=first){
    std::memset(row,0,(size_t)dst.width*px);
    continue;
   }
   if(first)std::memset(row,0,(size_t)first*px);
   if(last<dst.width)std::memset(row+(size_t)last*px,0,(size_t)(dst.width-last)*px);
   const uint8_t* source=inrow+(size_t)(first+shiftX)*px;
   if constexpr(Mode==Shape::Mapping){
    if constexpr(std::is_same_v<P,Pixel8>)
     simd::mapping8(row+(size_t)first*px,source,last-first,s,*packed,invScale,offset);
    else if constexpr(std::is_same_v<P,Pixel16>)
     simd::mapping16(row+(size_t)first*px,source,last-first,s,*packed16,invScale,offset);
    else
     simd::mapping32(row+(size_t)first*px,source,last-first,s,ramp,invScale,offset,direct);
   }else{
    simd::coveredRow<P,Mode>(row,source,y,dst,s,ramp,packed,packed16,
                             invLen,dx,dy,startAngle,invScale,offset,first,last,direct);
   }
   continue;
  }
#endif
  if constexpr(Mode!=Shape::Mapping){
   // Avoid a second full-row source-alpha read on non-horizontal gradients.
   // The covered AVX2 kernel specializes opaque blocks locally.
   if(inrow&&fullyInBounds&&
      (Mode==Shape::Linear && horizontal)){
    const uint8_t* pixels=inrow+(size_t)shiftX*sizeof(P);
    bool opaque=false;
    if constexpr(std::is_same_v<P,Pixel8>)opaque=allOpaque8(pixels,dst.width);
    else if constexpr(std::is_same_v<P,Pixel16>)opaque=allOpaque16(pixels,dst.width);
    else opaque=allOpaque32(pixels,dst.width);
    if(opaque){
     if constexpr(Mode==Shape::Linear){
      if(horizontal){
       if(opaqueTemplateRow<0){
#if defined(__AVX2__)
        simd::opaqueRow<P,Mode>(dst,y,s,ramp,packed,packed16,
                               invLen,dx,dy,startAngle,invScale,offset,direct);
#else
        for(int col=0;col<dst.width;col++){
         const float v=transform(a*(float)(col+dst.originX)-a*s.startX,
                                  invScale,offset,s.loop,s.reflect,s.invert);
         P pixel{};
         if constexpr(std::is_same_v<P,Pixel8>){
          const uint32_t sample=(*packed)[std::clamp((int)(v*K+.5f),0,K)];
          std::memcpy(&pixel,&sample,4);
         }else if constexpr(std::is_same_v<P,Pixel16>)pixel=lookup16(*packed16,v);
         else pixel=Ops<P>::output(lookup(ramp,v),P{1,0,0,0});
         std::memcpy(row+(size_t)col*sizeof(P),&pixel,sizeof(P));
        }
#endif
        opaqueTemplateRow=y;
       }else{
        std::memcpy(row,dst.data+(std::ptrdiff_t)opaqueTemplateRow*dst.rowBytes,
                    (size_t)dst.width*sizeof(P));
       }
       continue;
      }
     }
#if defined(__AVX2__)
     simd::opaqueRow<P,Mode>(dst,y,s,ramp,packed,packed16,
                            invLen,dx,dy,startAngle,invScale,offset,direct);
     continue;
#endif
    }
   }
  }
#if defined(__AVX2__)
  if constexpr(Mode!=Shape::Mapping){
   if(inrow&&fullyInBounds){
    simd::coveredRow<P,Mode>(row,inrow+(size_t)shiftX*sizeof(P),
                  y,dst,s,ramp,packed,packed16,
                  invLen,dx,dy,startAngle,invScale,offset,0,dst.width,direct);
    continue;
   }
  }
#endif
#if defined(__AVX2__)
  if constexpr(Mode==Shape::Mapping){
   if(inrow&&shiftX>=0&&shiftX+dst.width<=src.width){
    if constexpr(std::is_same_v<P,Pixel8>){
      simd::mapping8(row,inrow+(size_t)shiftX*4,dst.width,s,*packed,invScale,offset);
      continue;
    }else if constexpr(std::is_same_v<P,Pixel16>){
      simd::mapping16(row,inrow+(size_t)shiftX*8,dst.width,s,*packed16,invScale,offset);
      continue;
    }else if constexpr(std::is_same_v<P,Pixel32>){
      simd::mapping32(row,inrow+(size_t)shiftX*16,dst.width,s,ramp,invScale,offset,direct);
      continue;
    }
   }
  }
#endif
  const float fy=(float)(y+dst.originY);
  const float linearRow=b*(fy-s.startY)-a*s.startX;
  for(int x=0;x<dst.width;x++){
   int srcX=x+shiftX;
   P input{};
   if(inrow&&srcX>=0&&srcX<src.width)
    std::memcpy(&input,inrow+(size_t)srcX*sizeof(P),sizeof(P));
   const float fx=(float)(x+dst.originX);
   float t=0;
   if constexpr(Mode==Shape::Linear)
    t=a*fx+linearRow;
   else if constexpr(Mode==Shape::Radial){
    const float X=fx-s.startX,Y=fy-s.startY;
    t=std::sqrt(X*X+Y*Y)*invLen;
   }else if constexpr(Mode==Shape::Angle){
    t=(fastAtan2(fy-s.startY,fx-s.startX)-startAngle)*INV_TAU;
    // Angle by definition covers the entire [0,360) range even without Loop.
    t-=std::floor(t);
   }else if constexpr(Mode==Shape::Mapping)
    t=Ops<P>::luma(input);
   t=transform(t,invScale,offset,s.loop,s.reflect,s.invert);
   if constexpr(std::is_same_v<P,Pixel8>){
    // 8-bit output quantizes to 256 levels anyway. A 4097-entry ramp
    // avoids a second per-pixel floating-point interpolation.
    const int i=std::clamp((int)(t*K+.5f),0,K);
    uint32_t sample=(*packed)[i];
    if(input.a==255){
     std::memcpy(row+(size_t)x*sizeof(P),&sample,sizeof(sample));
    }else{
     P out{};
     for(int k=0;k<4;k++){
      unsigned v=(sample>>(k*8))&255u;
      ((uint8_t*)&out)[k]=(uint8_t)((v*input.a+127u)/255u);
     }
     std::memcpy(row+(size_t)x*sizeof(P),&out,sizeof(P));
    }
   }else if constexpr(std::is_same_v<P,Pixel16>){
    const int i=std::clamp((int)(t*K+.5f),0,K);
    Pixel16 sample=lookup16(*packed16,t);
    if(input.a!=32768){
      sample.a=(uint16_t)(((uint32_t)sample.a*input.a+16384u)>>15);
      sample.r=(uint16_t)(((uint32_t)sample.r*input.a+16384u)>>15);
      sample.g=(uint16_t)(((uint32_t)sample.g*input.a+16384u)>>15);
      sample.b=(uint16_t)(((uint32_t)sample.b*input.a+16384u)>>15);
    }
    std::memcpy(row+(size_t)x*sizeof(P),&sample,sizeof(sample));
   }else{
    P out=Ops<P>::output(direct?sampleDirectSrgb(*direct,t):lookup(ramp,t),input);
    std::memcpy(row+(size_t)x*sizeof(P),&out,sizeof(P));
   }
  }
 }
}
template<class P>
void dispatch(const Buffer& src,const WritableBuffer& dst,const Settings& s,
              const Ramp& ramp,const PackedRamp8* packed, const PackedRamp16* packed16,
              const blend::Config& blending,
              const DirectRamp* direct=nullptr) noexcept {
 switch(s.shape){
  case Shape::Linear:return renderImpl<P,Shape::Linear>(src,dst,s,ramp,packed,packed16,blending,direct);
  case Shape::Radial:return renderImpl<P,Shape::Radial>(src,dst,s,ramp,packed,packed16,blending,direct);
  case Shape::Angle:return renderImpl<P,Shape::Angle>(src,dst,s,ramp,packed,packed16,blending,direct);
  case Shape::Mapping:return renderImpl<P,Shape::Mapping>(src,dst,s,ramp,packed,packed16,blending,direct);
 }
}
enum class Depth{U8,U16,F32};
// Validate strides at the API boundary before any SIMD load/store. The host
// may provide cropped or bottom-up worlds with nontrivial or negative rowbytes.
inline bool validWorld(const Buffer& b,int pixelBytes) noexcept {
 if(b.width<0||b.height<0)return false;
 if(b.width==0||b.height==0)return true;
 if(!b.data||b.rowBytes==0)return false;
 return (int64_t)std::abs((int64_t)b.rowBytes)>=(int64_t)b.width*pixelBytes;
}
inline bool validWorld(const WritableBuffer& b,int pixelBytes) noexcept {
 if(b.width<0||b.height<0)return false;
 if(b.width==0||b.height==0)return true;
 if(!b.data||b.rowBytes==0)return false;
 return (int64_t)std::abs((int64_t)b.rowBytes)>=(int64_t)b.width*pixelBytes;
}
inline bool render(const Buffer& src,const WritableBuffer& dst,Depth depth,
                   const Settings& s,const Ramp& ramp,const PackedRamp8* packed,
                   const PackedRamp16* packed16,const blend::Config& blending={},
                   const DirectRamp* direct=nullptr){
 const int bpp=depth==Depth::U8?4:depth==Depth::U16?8:16;
 const bool fastGenerate=
   blending.color==blend::Mode::Replace &&
   blending.alpha==blend::Mode::Replace &&
   s.shape!=Shape::Mapping;
 const bool needsPacked=blending.legacyFast()||fastGenerate;
 if(!validWorld(src,bpp)||!validWorld(dst,bpp))return false;
 if(dst.width==0||dst.height==0)return true;
 switch(depth){
 case Depth::U8:if(needsPacked&&!packed)return false;dispatch<Pixel8>(src,dst,s,ramp,packed,packed16,blending,direct);break;
 case Depth::U16:if(needsPacked&&!packed16)return false;dispatch<Pixel16>(src,dst,s,ramp,packed,packed16,blending,direct);break;
 case Depth::F32:dispatch<Pixel32>(src,dst,s,ramp,packed,packed16,blending,direct);break;
 }
 return true;
}
}
