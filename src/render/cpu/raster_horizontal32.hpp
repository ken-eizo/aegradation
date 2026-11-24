#pragma once
// F32 horizontal Linear / Color Replace / Alpha Multiply specialization.
// Build exactly one 32KB SoA straight-RGBA gradient scanline per tile,
// then source-alpha multiply + 128-byte output writes per 8 pixels.
// This reuses the gradient sampling across ALL rows, avoiding LUT gathers,
// geometry transformations and color-space conversions per scanline.
// No global state, no AE hooks, no image-sized intermediates.
#include "raster_core.hpp"
#include "raster_parallel.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#if defined(__aarch64__) || defined(__arm64__)
#include <arm_neon.h>
#include <limits>
#endif
namespace gradation::raster::horizontal32 {
inline bool eligible(const Buffer& src,const WritableBuffer& dst,
                     const Settings& s,blend::Config cfg) noexcept {
 return src.data&&dst.data&&
        s.shape==Shape::Linear&&s.endY==s.startY&&
        cfg.color==blend::Mode::Replace&&
        cfg.alpha==blend::Mode::Multiply&&
        src.width==dst.width&&src.height==dst.height&&
        src.originX==dst.originX&&src.originY==dst.originY&&
        validWorld(src,16)&&validWorld(dst,16)&&
        src.rowBytes!=0&&dst.rowBytes!=0&&
        dst.width>=128&&dst.height>=16;
}
inline bool render(const Buffer& src,const WritableBuffer& dst,
                   const Settings& s,const Ramp& ramp,
                   const DirectRamp* direct=nullptr) {
 if(!eligible(src,dst,s,{blend::Mode::Replace,blend::Mode::Multiply,0}))
  return false;
 const int w=dst.width;
 std::vector<float> row((size_t)w*4);
 float* a=row.data(),*r=a+w,*g=r+w,*b=g+w;
 const float dx=s.endX-s.startX;
#if defined(__AVX2__)
 const float invLen=1.f/std::max(std::hypot(dx,0.f),1.e-6f);
#else
 const float invLen=1.f/std::max(std::sqrt(dx*dx+0.f),1.e-6f);
 const float aCoeff=dx*invLen*invLen;
#endif
 const float invScale=100.f/std::max(s.scalePercent,.0001f);
 const float offset=s.offsetPx*invLen;
 int x=0;
#if defined(__AVX2__)
 for(;x+8<=w;x+=8){
  const __m256 coord=simd::coordinate8<Shape::Linear>(
      x,0,dst,s,invLen,dx,0.f,0.f);
  const __m256 t=simd::vtransform(coord,s,invScale,offset);
  const simd::Channels c=simd::sample8<false,true>(ramp,t,direct);
  _mm256_storeu_ps(a+x,c.a);
  _mm256_storeu_ps(r+x,c.r);
  _mm256_storeu_ps(g+x,c.g);
  _mm256_storeu_ps(b+x,c.b);
 }
#endif
 for(;x<w;x++){
  const float fx=(float)(x+dst.originX)-s.startX;
#if defined(__AVX2__)
  const float coord=fx*dx*invLen*invLen;
#else
  const float coord=aCoeff*float(x+dst.originX)-aCoeff*s.startX;
#endif
  const float t=transform(coord,invScale,offset,s.loop,s.reflect,s.invert);
  const Color c=direct?sampleDirectSrgb(*direct,t):lookup(ramp,t);
  a[x]=clamp(c.a);r[x]=c.r;g[x]=c.g;b[x]=c.b;
 }
#if defined(__AVX2__)
 // Switch from channel-separated gather/transpose on EVERY row to
 // interleaved straight ARGB. Each 256-bit load contains two pixels;
 // broadcast their source alphas within 128-bit lanes. No output transpose.
 // Keep exact premultiplication order, including variable gradient alpha.
 if(w>=128&&dst.height>=16){
  std::vector<Pixel32> interleaved((size_t)w);
  bool opaque=true;
  for(int j=0;j<w;j++){
   interleaved[j]={a[j],r[j],g[j],b[j]};
   opaque&=a[j]==1.f;
  }
  const auto zero=_mm256_setzero_ps(),one=_mm256_set1_ps(1.f);
  const auto limit=_mm256_set1_ps(1.e8f);
  const bool useNT=(uint64_t)w*dst.height*16>=16ull*1024*1024&&
     dst.rowBytes>0&&src.data!=dst.data;
  auto rows=[&](int begin,int end){
  for(int y=begin;y<end;y++){
  const uint8_t* in=src.data+(std::ptrdiff_t)y*src.rowBytes;
   uint8_t* out=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
   auto scalar=[&](int j){
    Pixel32 source{};std::memcpy(&source,in+(size_t)j*16,16);
    const Pixel32 result=Ops<Pixel32>::output({r[j],g[j],b[j],a[j]},source);
    std::memcpy(out+(size_t)j*16,&result,16);
   };
   auto pair=[&](int j,bool nt){
    auto sa=_mm256_loadu_ps((const float*)(in+(size_t)j*16));
    sa=_mm256_shuffle_ps(sa,sa,0x00);
    sa=_mm256_blendv_ps(zero,sa,_mm256_cmp_ps(simd::vabs(sa),limit,_CMP_LT_OQ));
    sa=_mm256_max_ps(zero,_mm256_min_ps(one,sa));
    auto g=_mm256_loadu_ps((const float*)(interleaved.data()+j));
    if(!opaque){
     sa=_mm256_mul_ps(sa,_mm256_shuffle_ps(g,g,0x00));
     g=_mm256_blend_ps(g,one,0x11);
    }
    const auto v=_mm256_mul_ps(g,sa);
    if(nt)_mm256_stream_ps((float*)(out+(size_t)j*16),v);
    else _mm256_storeu_ps((float*)(out+(size_t)j*16),v);
   };
   int j=0;
   // AE buffers can have arbitrary pitches and addresses. Only use NT
   // for complete aligned cache lines; otherwise use ordinary stores.
   if(useNT&&(reinterpret_cast<uintptr_t>(out)&15)==0){
    for(;j<w&&(reinterpret_cast<uintptr_t>(out+(size_t)j*16)&63);j++)scalar(j);
    for(;j+4<=w;j+=4){pair(j,true);pair(j+2,true);}
   }
   for(;j+2<=w;j+=2)pair(j,false);
   for(;j<w;j++)scalar(j);
  }
  if(useNT)_mm_sfence();
  };
  // Independent stripes with a bounded shared budget; no waiting for
  // another MFR frame and no host calls from workers. Keep stack data
  // alive until every native worker is joined.
  const int workers=useNT&&parallel::mayAddWorkers()?parallel::configuredWorkers():1;
  if(workers>1&&!parallel::active.test_and_set(std::memory_order_acquire)){
   struct Release{~Release(){parallel::active.clear(std::memory_order_release);}} release;
   const int n=std::min(workers,4);
   auto stripe=[&](int k)noexcept{rows(k*dst.height/n,(k+1)*dst.height/n);};
   parallel::runStripes(n,stripe);
  }else rows(0,dst.height);
  return true;
 }
#endif
 for(int y=0;y<dst.height;y++){
  const uint8_t* in=src.data+(std::ptrdiff_t)y*src.rowBytes;
  uint8_t* out=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
  int i=0;
#if defined(__AVX2__)
  for(;i+8<=w;i+=8){
   const __m256 source=simd::alpha8Float(in+(size_t)i*16);
   const __m256 filtered=_mm256_blendv_ps(
      _mm256_setzero_ps(),source,
      _mm256_cmp_ps(simd::vabs(source),_mm256_set1_ps(1.e8f),_CMP_LT_OQ));
   const __m256 clamped=_mm256_max_ps(
      _mm256_setzero_ps(),_mm256_min_ps(_mm256_set1_ps(1.f),filtered));
   const __m256 alpha=_mm256_mul_ps(clamped,_mm256_loadu_ps(a+i));
   simd::storeFloatARGB8(out+(size_t)i*16,
     alpha,
     _mm256_mul_ps(alpha,_mm256_loadu_ps(r+i)),
     _mm256_mul_ps(alpha,_mm256_loadu_ps(g+i)),
     _mm256_mul_ps(alpha,_mm256_loadu_ps(b+i)));
  }
#endif
#if defined(__aarch64__) || defined(__arm64__)
  for(;i+4<=w;i+=4){
   const auto input=vld4q_f32(reinterpret_cast<const float*>(in+(size_t)i*16));
   const auto zero=vdupq_n_f32(0.f),one=vdupq_n_f32(1.f);
   auto sa=vbslq_f32(vcleq_f32(vabsq_f32(input.val[0]),vdupq_n_f32(std::numeric_limits<float>::max())),input.val[0],zero);
   sa=vmaxq_f32(zero,vminq_f32(one,sa));
   const auto alpha=vmulq_f32(sa,vld1q_f32(a+i));
   float32x4x4_t value{};
   value.val[0]=alpha;
   value.val[1]=vmulq_f32(alpha,vld1q_f32(r+i));
   value.val[2]=vmulq_f32(alpha,vld1q_f32(g+i));
   value.val[3]=vmulq_f32(alpha,vld1q_f32(b+i));
   vst4q_f32(reinterpret_cast<float*>(out+(size_t)i*16),value);
  }
#endif
  for(;i<w;i++){
   Pixel32 input{};
   std::memcpy(&input,in+(size_t)i*16,16);
   const Color c{r[i],g[i],b[i],a[i]};
   const Pixel32 result=Ops<Pixel32>::output(c,input);
   std::memcpy(out+(size_t)i*16,&result,16);
  }
 }
 return true;
}
} // namespace gradation::raster::horizontal32
