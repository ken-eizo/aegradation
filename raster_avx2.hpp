#pragma once
// Eight-wide AVX2 coordinate generation and output for opaque non-Mapping tiles.
// No shared mutable state, no heap allocation, no AE calls.
#if defined(__AVX2__)
#include <immintrin.h>
namespace simd {
inline __m256 vabs(__m256 x) noexcept{
 return _mm256_andnot_ps(_mm256_set1_ps(-0.0f),x);
}
// Approximate AVX reciprocal refined with one Newton-Raphson step.
// Relative error is near single-precision round-off, not the raw rcp ~1e-4.
inline __m256 reciprocalNR(__m256 d) noexcept {
 const __m256 guess=_mm256_rcp_ps(d);
 return _mm256_mul_ps(guess,_mm256_sub_ps(_mm256_set1_ps(2.f),
                               _mm256_mul_ps(d,guess)));
}
inline __m256 divideNR(__m256 numerator,__m256 denominator) noexcept {
 return _mm256_mul_ps(numerator,reciprocalNR(denominator));
}
inline __m256 vatan(__m256 z) noexcept {
 const __m256 zz=_mm256_mul_ps(z,z);
 __m256 a=_mm256_set1_ps(-.013482107780873775f);
 a=_mm256_add_ps(_mm256_mul_ps(a,zz),_mm256_set1_ps(.05748184770345688f));
 a=_mm256_add_ps(_mm256_mul_ps(a,zz),_mm256_set1_ps(-.1212436482310295f));
 a=_mm256_add_ps(_mm256_mul_ps(a,zz),_mm256_set1_ps(.1956379562616348f));
 a=_mm256_add_ps(_mm256_mul_ps(a,zz),_mm256_set1_ps(-.3329949676990509f));
 a=_mm256_add_ps(_mm256_mul_ps(a,zz),_mm256_set1_ps(.9999956488609314f));
 return _mm256_mul_ps(z,a);
}
inline __m256 vatan2(__m256 y,__m256 x) noexcept {
 const __m256 zero=_mm256_setzero_ps(), pi=_mm256_set1_ps(3.14159265358979323846f);
 const __m256 half=_mm256_set1_ps(1.57079632679489661923f), eps=_mm256_set1_ps(1e-20f);
 const __m256 ax=vabs(x),ay=vabs(y);
 const __m256 swap=_mm256_cmp_ps(ax,ay,_CMP_LT_OQ);
 const __m256 numerator=_mm256_blendv_ps(ay,ax,swap);
 const __m256 denominator=_mm256_max_ps(_mm256_blendv_ps(ax,ay,swap),eps);
 const __m256 z=divideNR(numerator,denominator);
 __m256 theta=vatan(z);
 theta=_mm256_blendv_ps(theta,_mm256_sub_ps(half,theta),swap);
 theta=_mm256_blendv_ps(theta,_mm256_sub_ps(pi,theta),_mm256_cmp_ps(x,zero,_CMP_LT_OQ));
 theta=_mm256_blendv_ps(theta,_mm256_sub_ps(zero,theta),_mm256_cmp_ps(y,zero,_CMP_LT_OQ));
 return _mm256_blendv_ps(zero,theta,_mm256_cmp_ps(_mm256_add_ps(ax,ay),eps,_CMP_GE_OQ));
}
// For an image scanline y is constant across all eight lanes.
// Preserve polynomial, reciprocal, quadrant, and center-mask semantics.
// Min/max replace comparison-dependent operand blends.
inline __m256 vatan2Row(float y,__m256 x) noexcept {
#if defined(GRADATION_ANGLE_ROW_BASELINE)
 return vatan2(_mm256_set1_ps(y),x);
#else
 const auto zero=_mm256_setzero_ps(),vy=_mm256_set1_ps(y);
 const auto ax=vabs(x),ay=vabs(vy);
 const auto eps=_mm256_set1_ps(1e-20f);
 const auto swap=_mm256_cmp_ps(ax,ay,_CMP_LT_OQ);
 const auto z=divideNR(_mm256_min_ps(ax,ay),_mm256_max_ps(_mm256_max_ps(ax,ay),eps));
 auto theta=vatan(z);
 theta=_mm256_blendv_ps(theta,_mm256_sub_ps(_mm256_set1_ps(1.57079632679489661923f),theta),swap);
 theta=_mm256_blendv_ps(theta,_mm256_sub_ps(_mm256_set1_ps(3.14159265358979323846f),theta),
                      _mm256_cmp_ps(x,zero,_CMP_LT_OQ));
 theta=_mm256_blendv_ps(theta,_mm256_sub_ps(zero,theta),_mm256_cmp_ps(vy,zero,_CMP_LT_OQ));
 return _mm256_blendv_ps(zero,theta,_mm256_cmp_ps(_mm256_add_ps(ax,ay),eps,_CMP_GE_OQ));
#endif
}
inline __m256 vtransform(__m256 t,const Settings& s,float invScale,float offset) noexcept {
 const __m256 one=_mm256_set1_ps(1.f),zero=_mm256_setzero_ps();
 // Dominant default transform: scale=100%, offset=0, no loop/reflect/
 // invert. Skip all 2nd-stage sanitization/multiply/add/floor operations.
 // Keep the SAME original finite-range gate and clip, bit-for-bit.
 if(invScale==1.f&&offset==0.f&&!s.loop&&!s.reflect&&!s.invert){
  t=_mm256_blendv_ps(zero,t,_mm256_cmp_ps(vabs(t),_mm256_set1_ps(1.e8f),_CMP_LT_OQ));
  return _mm256_max_ps(zero,_mm256_min_ps(one,t));
 }
 t=_mm256_blendv_ps(zero,t,_mm256_cmp_ps(vabs(t),_mm256_set1_ps(1.e8f),_CMP_LT_OQ));
 t=_mm256_add_ps(_mm256_mul_ps(t,_mm256_set1_ps(invScale)),_mm256_set1_ps(offset));
 t=_mm256_blendv_ps(zero,t,_mm256_cmp_ps(vabs(t),_mm256_set1_ps(1.e8f),_CMP_LT_OQ));
 if(s.reflect){
  t=_mm256_sub_ps(t,_mm256_mul_ps(_mm256_set1_ps(2.f),
             _mm256_floor_ps(_mm256_mul_ps(t,_mm256_set1_ps(.5f)))));
  t=_mm256_sub_ps(one,vabs(_mm256_sub_ps(one,t)));
 }else if(s.loop){
  t=_mm256_sub_ps(t,_mm256_floor_ps(t));
 }else{
  t=_mm256_max_ps(zero,_mm256_min_ps(one,t));
 }
 return s.invert?_mm256_sub_ps(one,t):t;
}
template<Shape Mode,bool AngleRow=false>
inline __m256 coordinate8(int x,int y,const WritableBuffer& dst,
 const Settings& s,float invLen,float dx,float dy,float startAngle) noexcept {
 const __m256 lane=_mm256_setr_ps(0,1,2,3,4,5,6,7);
 const __m256 vx=_mm256_add_ps(lane,_mm256_set1_ps((float)(x+dst.originX)-s.startX));
 const __m256 vy=_mm256_set1_ps((float)(y+dst.originY)-s.startY);
 if constexpr (Mode==Shape::Linear){
  const float invLen2=invLen*invLen;
  return _mm256_mul_ps(_mm256_add_ps(_mm256_mul_ps(vx,_mm256_set1_ps(dx)),
         _mm256_mul_ps(vy,_mm256_set1_ps(dy))),_mm256_set1_ps(invLen2));
 }else if constexpr (Mode==Shape::Radial){
  return _mm256_mul_ps(_mm256_sqrt_ps(_mm256_add_ps(_mm256_mul_ps(vx,vx),
                                _mm256_mul_ps(vy,vy))),_mm256_set1_ps(invLen));
 }else{
  constexpr float INV_TAU=1.f/6.28318530717958647692f;
  const float yy=(float)(y+dst.originY)-s.startY;
  const auto theta=AngleRow?vatan2Row(yy,vx):vatan2(vy,vx);
  __m256 t=_mm256_mul_ps(_mm256_sub_ps(theta,
                            _mm256_set1_ps(startAngle)),_mm256_set1_ps(INV_TAU));
  return _mm256_sub_ps(t,_mm256_floor_ps(t));
 }
}
// Eight contiguous AE ARGB32 pixels. Use full-width AVX2 interleaving;
// four 32-byte writes instead of eight SSE 16-byte writes. All depth-32
// generator/mapping/blend paths share this exact bit-preserving store.
inline void storeFloatARGB8(uint8_t* out,__m256 a,__m256 r,
                            __m256 g,__m256 b) noexcept {
 const __m256 ar0=_mm256_unpacklo_ps(a,r);
 const __m256 ar1=_mm256_unpackhi_ps(a,r);
 const __m256 gb0=_mm256_unpacklo_ps(g,b);
 const __m256 gb1=_mm256_unpackhi_ps(g,b);
 const __m256 p04=_mm256_shuffle_ps(ar0,gb0,0x44);
 const __m256 p15=_mm256_shuffle_ps(ar0,gb0,0xEE);
 const __m256 p26=_mm256_shuffle_ps(ar1,gb1,0x44);
 const __m256 p37=_mm256_shuffle_ps(ar1,gb1,0xEE);
 _mm256_storeu_ps((float*)(out+0),_mm256_permute2f128_ps(p04,p15,0x20));
 _mm256_storeu_ps((float*)(out+32),_mm256_permute2f128_ps(p26,p37,0x20));
 _mm256_storeu_ps((float*)(out+64),_mm256_permute2f128_ps(p04,p15,0x31));
 _mm256_storeu_ps((float*)(out+96),_mm256_permute2f128_ps(p26,p37,0x31));
}
// Four channels of floating point LUT, gathered eight pixels per instruction.
// Use linear interpolation between the 4096 subintervals: do not quantize 32bpc
// to the 12-bit nearest-neighbor output used by the integer formats.
template<bool PackedMapping=false,bool TrustedT=false>
inline void writeFloat8(uint8_t* out,const Ramp& ramp,__m256 t,
                          __m256 srcAlpha=_mm256_set1_ps(1.f),
                          const DirectRamp* direct=nullptr) noexcept {
 srcAlpha=_mm256_blendv_ps(_mm256_setzero_ps(),srcAlpha,
              _mm256_cmp_ps(vabs(srcAlpha),_mm256_set1_ps(1.e8f),_CMP_LT_OQ));
 const __m256 clampedSrc=_mm256_max_ps(_mm256_setzero_ps(),
                        _mm256_min_ps(_mm256_set1_ps(1.f),srcAlpha));
 __m256 alpha{},r{},g{},b{};
 if(direct){
  // sRGB straight channels interpolate linearly between the actual stops.
  // Avoid 8 expensive random gathers per 8 pixels; 32-bit float keeps
  // arithmetic precision. Other interpolation spaces stay on the LUT.
  const __m256 u=TrustedT?t:_mm256_max_ps(_mm256_setzero_ps(),
                       _mm256_min_ps(_mm256_set1_ps(1.f),t));
  const bool segmented=direct->count==3;
  const __m256 split=_mm256_set1_ps(direct->middlePosition);
  const __m256 leftMask=_mm256_cmp_ps(u,split,_CMP_LT_OQ);
  const __m256 firstPart=segmented?
     _mm256_min_ps(_mm256_set1_ps(1.f),
       _mm256_max_ps(_mm256_setzero_ps(),
          _mm256_mul_ps(u,_mm256_set1_ps(1.f/direct->middlePosition)))):
     u;
  const __m256 lastPart=segmented?
     _mm256_min_ps(_mm256_set1_ps(1.f),
       _mm256_max_ps(_mm256_setzero_ps(),
          _mm256_mul_ps(_mm256_sub_ps(u,split),
                        _mm256_set1_ps(1.f/(1.f-direct->middlePosition))))):
     u;
  auto linear=[&](float start,float end,float mid){
   if(!segmented)
    return _mm256_add_ps(_mm256_set1_ps(start),
             _mm256_mul_ps(u,_mm256_set1_ps(end-start)));
   const __m256 l=_mm256_add_ps(_mm256_set1_ps(start),
                          _mm256_mul_ps(firstPart,_mm256_set1_ps(mid-start)));
   const __m256 r=_mm256_add_ps(_mm256_set1_ps(mid),
                          _mm256_mul_ps(lastPart,_mm256_set1_ps(end-mid)));
   return _mm256_blendv_ps(r,l,leftMask);
  };
  // Factory-default white-to-black sRGB ramp: all stops have
  // r==g==b and a==1. Recompute neither the same grayscale interpolation
  // three times nor the constant alpha. This applies to any 2/3-stop
  // grayscale ramp and preserves exact straight/premultiplied floats.
  const bool opaque=(direct->first.a==1.f&&direct->last.a==1.f&&
             (!segmented||direct->middle.a==1.f));
  alpha=opaque?clampedSrc:
    _mm256_mul_ps(clampedSrc,
      _mm256_max_ps(_mm256_setzero_ps(),_mm256_min_ps(_mm256_set1_ps(1.f),
          linear(direct->first.a,direct->last.a,direct->middle.a))));
#if !defined(GRADATION_DISABLE_F32_GRAY_FAST)
  // Compare channel BITS, not floating ==. In 32bpc a channel may
  // contain -0 while another contains +0: numerically equal channels
  // could still differ in IEEE-754 output bits after premultiplication.
  const auto sameBits=[](float a,float b) noexcept {
   uint32_t lhs=0,rhs=0;
   std::memcpy(&lhs,&a,sizeof(lhs));
   std::memcpy(&rhs,&b,sizeof(rhs));
   return lhs==rhs;
  };
  const bool gray=(sameBits(direct->first.r,direct->first.g)&&
       sameBits(direct->first.r,direct->first.b)&&
       sameBits(direct->last.r,direct->last.g)&&
       sameBits(direct->last.r,direct->last.b)&&
       (!segmented||(sameBits(direct->middle.r,direct->middle.g)&&
                     sameBits(direct->middle.r,direct->middle.b))));
  if(gray){
   r=_mm256_mul_ps(alpha,
       linear(direct->first.r,direct->last.r,direct->middle.r));
   g=r;b=r;
  }else
#endif
  {
   r=_mm256_mul_ps(alpha,linear(direct->first.r,direct->last.r,direct->middle.r));
   g=_mm256_mul_ps(alpha,linear(direct->first.g,direct->last.g,direct->middle.g));
   b=_mm256_mul_ps(alpha,linear(direct->first.b,direct->last.b,direct->middle.b));
  }
 }else{
 const __m256 zero=_mm256_setzero_ps(),one=_mm256_set1_ps(4096.f);
 const __m256 coord=_mm256_mul_ps(TrustedT?t:_mm256_max_ps(zero,
                  _mm256_min_ps(_mm256_set1_ps(1.f),t)),one);
 const __m256 val=_mm256_min_ps(coord,_mm256_set1_ps(4095.f));
 const __m256i index=_mm256_cvttps_epi32(val);
 const __m256 fraction=_mm256_sub_ps(coord,_mm256_cvtepi32_ps(index));
#if !defined(GRADATION_F32_LUT_GATHER_BASELINE)
 if constexpr(PackedMapping){
 // Each ramp entry is already a contiguous RGBA vector. Interpolate
 // two pixels per AVX register, then rotate RGBA to AE's ARGB.
 // No channel-wise gathers or final eight-pixel transpose.
 alignas(32) int indices[8];
 alignas(32) float fractions[8],sourceAlpha[8];
 _mm256_store_si256((__m256i*)indices,index);
 _mm256_store_ps(fractions,fraction);
 _mm256_store_ps(sourceAlpha,clampedSrc);
 const float* ptr=reinterpret_cast<const float*>(ramp.data());
 const auto one4=_mm256_set1_ps(1.f);
 const auto pair=[&](int i){
  const float* p0=ptr+4*indices[i],*p1=ptr+4*indices[i+1];
  const auto l=_mm256_insertf128_ps(_mm256_castps128_ps256(_mm_loadu_ps(p0)),_mm_loadu_ps(p1),1);
  const auto r=_mm256_insertf128_ps(_mm256_castps128_ps256(_mm_loadu_ps(p0+4)),_mm_loadu_ps(p1+4),1);
  const auto t=_mm256_insertf128_ps(_mm256_castps128_ps256(_mm_set1_ps(fractions[i])),_mm_set1_ps(fractions[i+1]),1);
  const auto c=_mm256_add_ps(l,_mm256_mul_ps(t,_mm256_sub_ps(r,l)));
  const auto sa=_mm256_insertf128_ps(_mm256_castps128_ps256(_mm_set1_ps(sourceAlpha[i])),_mm_set1_ps(sourceAlpha[i+1]),1);
  const auto a=_mm256_mul_ps(sa,_mm256_max_ps(zero,_mm256_min_ps(one4,_mm256_shuffle_ps(c,c,0xff))));
  const auto v=_mm256_blend_ps(_mm256_mul_ps(a,c),a,0x88);
  _mm256_storeu_ps((float*)(out+i*16),_mm256_shuffle_ps(v,v,0x93));
 };
 pair(0);pair(2);pair(4);pair(6);
 return;
 }
#endif
 const __m256i base=_mm256_slli_epi32(index,2);
 const float* ptr=reinterpret_cast<const float*>(ramp.data());
 auto load=[&](__m256i offsets){
  const __m256 left=_mm256_i32gather_ps(ptr,offsets,4);
  const __m256 right=_mm256_i32gather_ps(ptr,_mm256_add_epi32(offsets,_mm256_set1_epi32(4)),4);
  return _mm256_add_ps(left,_mm256_mul_ps(fraction,_mm256_sub_ps(right,left)));
 };
  alpha=_mm256_mul_ps(clampedSrc,
              _mm256_max_ps(_mm256_setzero_ps(),
              _mm256_min_ps(_mm256_set1_ps(1.f),
                load(_mm256_add_epi32(base,_mm256_set1_epi32(3))))));
  r=_mm256_mul_ps(alpha,load(base));
  g=_mm256_mul_ps(alpha,load(_mm256_add_epi32(base,_mm256_set1_epi32(1))));
  b=_mm256_mul_ps(alpha,load(_mm256_add_epi32(base,_mm256_set1_epi32(2))));
 }
#if defined(GRADATION_F32_OLD_TRANSPOSE)
 __m128 a0=_mm256_castps256_ps128(alpha),r0=_mm256_castps256_ps128(r);
 __m128 g0=_mm256_castps256_ps128(g),b0=_mm256_castps256_ps128(b);
 _MM_TRANSPOSE4_PS(a0,r0,g0,b0);
 _mm_storeu_ps((float*)(out+0),a0);
 _mm_storeu_ps((float*)(out+16),r0);
 _mm_storeu_ps((float*)(out+32),g0);
 _mm_storeu_ps((float*)(out+48),b0);
 __m128 a1=_mm256_extractf128_ps(alpha,1),r1=_mm256_extractf128_ps(r,1);
 __m128 g1=_mm256_extractf128_ps(g,1),b1=_mm256_extractf128_ps(b,1);
 _MM_TRANSPOSE4_PS(a1,r1,g1,b1);
 _mm_storeu_ps((float*)(out+64),a1);
 _mm_storeu_ps((float*)(out+80),r1);
 _mm_storeu_ps((float*)(out+96),g1);
 _mm_storeu_ps((float*)(out+112),b1);
#else
 storeFloatARGB8(out,alpha,r,g,b);
#endif
}
// Map an incoming 8-bit ARGB tile. All pixel colors matter; no opaque shortcut.
inline void mapping8(uint8_t* dst,const uint8_t* src,int n,
                     const Settings& settings,const PackedRamp8& lut,
                     float invScale,float offset) noexcept {
 const __m256i mask=_mm256_set1_epi32(255);
 const __m256 p255=_mm256_set1_ps(1.f/255.f);
 const __m256 one=_mm256_set1_ps(1.f);
 int x=0;
 for(;x+8<=n;x+=8){
  const __m256i orig=_mm256_loadu_si256((const __m256i*)(src+(size_t)x*4));
  const __m256i a=_mm256_and_si256(orig,mask);
  const __m256i r=_mm256_and_si256(_mm256_srli_epi32(orig,8),mask);
  const __m256i g=_mm256_and_si256(_mm256_srli_epi32(orig,16),mask);
  const __m256i b=_mm256_srli_epi32(orig,24);
  __m256 y=_mm256_mul_ps(_mm256_cvtepi32_ps(r),_mm256_set1_ps(.2126f));
  y=_mm256_add_ps(y,_mm256_mul_ps(_mm256_cvtepi32_ps(g),_mm256_set1_ps(.7152f)));
  y=_mm256_add_ps(y,_mm256_mul_ps(_mm256_cvtepi32_ps(b),_mm256_set1_ps(.0722f)));
  y=divideNR(y,_mm256_cvtepi32_ps(_mm256_max_epi32(a,_mm256_set1_epi32(1))));
  y=_mm256_max_ps(_mm256_setzero_ps(),_mm256_min_ps(one,y));
  y=vtransform(y,settings,invScale,offset);
  __m256i indices=_mm256_cvtps_epi32(_mm256_mul_ps(y,_mm256_set1_ps(4096.f)));
  indices=_mm256_max_epi32(_mm256_setzero_si256(),
                           _mm256_min_epi32(indices,_mm256_set1_epi32(4096)));
  __m256i c=_mm256_i32gather_epi32((const int*)lut.data(),indices,4);
  const __m256i repeatA=_mm256_or_si256(a,_mm256_slli_epi32(a,16));
  const __m256i low=_mm256_mullo_epi16(_mm256_and_si256(c,_mm256_set1_epi32(0x00ff00ff)),repeatA);
  const __m256i high=_mm256_mullo_epi16(_mm256_and_si256(_mm256_srli_epi32(c,8),_mm256_set1_epi32(0x00ff00ff)),repeatA);
  // 255 division with correct rounding for all integer RGB components.
  auto div255=[](__m256i z){
    z=_mm256_add_epi16(z,_mm256_set1_epi16(128));
    z=_mm256_add_epi16(z,_mm256_srli_epi16(z,8));
    return _mm256_and_si256(_mm256_srli_epi16(z,8),_mm256_set1_epi32(0x00ff00ff));
  };
  const __m256i packed=_mm256_or_si256(div255(low),_mm256_slli_epi32(div255(high),8));
  _mm256_storeu_si256((__m256i*)(dst+(size_t)x*4),packed);
 }
 for(;x<n;x++){
  Pixel8 in;std::memcpy(&in,src+(size_t)x*4,4);
  float luminance=Ops<Pixel8>::luma(in);
  float t=transform(luminance,invScale,offset,settings.loop,settings.reflect,settings.invert);
  uint32_t sample=lut[std::clamp((int)(t*4096.f+.5f),0,4096)];
  Pixel8 out;
  for(int ch=0;ch<4;ch++){
   unsigned v=(sample>>(8*ch))&255u;
   ((uint8_t*)&out)[ch]=(uint8_t)((v*in.a+127u)/255u);
  }
  std::memcpy(dst+(size_t)x*4,&out,4);
 }
}
// Full Q16 linear interpolation of 4 packed ARGB16 pixels per AVX2 register.
// Each of 16 channels is processed independently, preserving 15-bit ramps.
inline __m256i lerp4x16(__m256i left,__m256i right,__m128i frac32) noexcept {
#if defined(GRADATION_LEGACY_Q16_BROADCAST)
 const __m256i f64=_mm256_cvtepu32_epi64(frac32);
 const __m256i lower=_mm256_mul_epu32(f64,_mm256_set1_epi32(0x00010001));
 const __m256i frac16=_mm256_or_si256(lower,_mm256_slli_epi64(lower,32));
#else
 // Every fraction is Q16 in [0,65535]. Replicate its two low bytes
 // directly to all ARGB channels, instead of multiplying by 0x10001.
 const auto bytes=_mm256_broadcastsi128_si256(frac32);
 const auto order=_mm256_setr_epi8(
  0,1,0,1,0,1,0,1,4,5,4,5,4,5,4,5,
  8,9,8,9,8,9,8,9,12,13,12,13,12,13,12,13);
 const auto frac16=_mm256_shuffle_epi8(bytes,order);
#endif
 const __m256i leftContribution=_mm256_mulhi_epu16(left,frac16);
 const __m256i rightContribution=_mm256_mulhi_epu16(right,frac16);
 return _mm256_add_epi16(_mm256_sub_epi16(left,leftContribution),rightContribution);
}
inline __m256i sample4x16(const PackedRamp16& ramp,__m128i idx,
                          __m128i frac) noexcept {
 const __m256i a=_mm256_i32gather_epi64((const long long*)ramp.data(),idx,8);
 const __m128i next=_mm_add_epi32(idx,_mm_set1_epi32(1));
 const __m256i b=_mm256_i32gather_epi64((const long long*)ramp.data(),next,8);
 return lerp4x16(a,b,frac);
}
// 16-bit mapping, eight input pixels per step.
// Preserves 15-bit AE channel precision with rounded Q15 alpha multiplication.
// VPMULHRSW rounds the signed Q15 product in one instruction.
// Restore the unsigned high-bit contribution modulo 65536; this also
// handles the full AE white endpoint 32768 and out-of-range uint16 input.
inline __m256i mulQ15Unsigned(__m256i color,__m256i alpha) noexcept {
 const auto signedProduct=_mm256_mulhrs_epi16(color,alpha);
 const auto correctAlpha=_mm256_and_si256(_mm256_srai_epi16(alpha,15),_mm256_slli_epi16(color,1));
 const auto correctColor=_mm256_and_si256(_mm256_srai_epi16(color,15),_mm256_slli_epi16(alpha,1));
 return _mm256_add_epi16(_mm256_add_epi16(signedProduct,correctAlpha),correctColor);
}
inline __m256i replicateAlpha16(__m256i source) noexcept {
 const __m256i pick=_mm256_setr_epi8(
    0,1,0,1,0,1,0,1,8,9,8,9,8,9,8,9,
    0,1,0,1,0,1,0,1,8,9,8,9,8,9,8,9);
 return _mm256_shuffle_epi8(source,pick);
}
// Prevalidated scanline values have no high bit: eliminate one
// unsigned correction in the hot loop, not a precision approximation.
inline __m256i mulSourceAlpha16Low(__m256i color,__m256i source) noexcept {
 const auto alpha=replicateAlpha16(source);
 return _mm256_add_epi16(_mm256_mulhrs_epi16(color,alpha),
    _mm256_and_si256(_mm256_srai_epi16(alpha,15),_mm256_slli_epi16(color,1)));
}
inline __m256i mulSourceAlpha16(__m256i color,__m256i source) noexcept {
 const __m256i pick=_mm256_setr_epi8(
    0,1,0,1,0,1,0,1,8,9,8,9,8,9,8,9,
    0,1,0,1,0,1,0,1,8,9,8,9,8,9,8,9);
 const __m256i alpha=_mm256_shuffle_epi8(source,pick);
 const __m256i low=_mm256_mullo_epi16(color,alpha);
 const __m256i high=_mm256_mulhi_epu16(color,alpha);
 const __m256i lo15=_mm256_srli_epi16(low,15);
 const __m256i round=_mm256_and_si256(_mm256_srli_epi16(low,14),_mm256_set1_epi16(1));
 return _mm256_add_epi16(_mm256_add_epi16(_mm256_slli_epi16(high,1),lo15),round);
}
inline void mapping16(uint8_t* dst,const uint8_t* src,int n,
                      const Settings& settings,const PackedRamp16& lut,
                      float invScale,float offset) noexcept {
 const __m256i indices=_mm256_setr_epi32(0,2,4,6,8,10,12,14);
 const __m256i mask=_mm256_set1_epi32(0xffff);
 const __m256 fmax=_mm256_set1_ps(1.f),zero=_mm256_setzero_ps();
 int x=0;
 for(;x+8<=n;x+=8){
  const int* source=(const int*)(src+(size_t)x*8);
  // Two contiguous loads replace two eight-lane gathers and the
  // redundant source loads used later for alpha multiplication.
  const __m256i source0=_mm256_loadu_si256((const __m256i*)source);
  const __m256i source1=_mm256_loadu_si256((const __m256i*)(source+8));
#if defined(GRADATION_MAPPING16_GATHER_BASELINE)
  const __m256i ar=_mm256_i32gather_epi32(source,indices,4);
  const __m256i gb=_mm256_i32gather_epi32(source+1,indices,4);
#else
  const __m256i order=_mm256_setr_epi32(0,2,4,6,1,3,5,7);
  const __m256i lo=_mm256_permutevar8x32_epi32(source0,order);
  const __m256i hi=_mm256_permutevar8x32_epi32(source1,order);
  const __m256i ar=_mm256_permute2x128_si256(lo,hi,0x20);
  const __m256i gb=_mm256_permute2x128_si256(lo,hi,0x31);
#endif
  const __m256i al=_mm256_and_si256(ar,mask);
  const __m256i red=_mm256_srli_epi32(ar,16);
  const __m256i green=_mm256_and_si256(gb,mask);
  const __m256i blue=_mm256_srli_epi32(gb,16);
  __m256 lum=_mm256_mul_ps(_mm256_cvtepi32_ps(red),_mm256_set1_ps(.2126f));
  lum=_mm256_add_ps(lum,_mm256_mul_ps(_mm256_cvtepi32_ps(green),_mm256_set1_ps(.7152f)));
  lum=_mm256_add_ps(lum,_mm256_mul_ps(_mm256_cvtepi32_ps(blue),_mm256_set1_ps(.0722f)));
  lum=divideNR(lum,_mm256_cvtepi32_ps(_mm256_max_epi32(al,_mm256_set1_epi32(1))));
  lum=_mm256_max_ps(zero,_mm256_min_ps(fmax,lum));
  lum=vtransform(lum,settings,invScale,offset);
  const __m256 coord=_mm256_mul_ps(lum,_mm256_set1_ps(4096.f));
  const __m256i idx=_mm256_cvttps_epi32(_mm256_min_ps(coord,_mm256_set1_ps(4095.f)));
  const __m256i frac=_mm256_cvtps_epi32(_mm256_mul_ps(
               _mm256_sub_ps(coord,_mm256_cvtepi32_ps(idx)),_mm256_set1_ps(65535.f)));
  const __m256i a=sample4x16(lut,_mm256_castsi256_si128(idx),_mm256_castsi256_si128(frac));
  const __m256i b=sample4x16(lut,_mm256_extracti128_si256(idx,1),_mm256_extracti128_si256(frac,1));

  _mm256_storeu_si256((__m256i*)(dst+(size_t)x*8),mulSourceAlpha16(a,source0));
  _mm256_storeu_si256((__m256i*)(dst+(size_t)(x+4)*8),mulSourceAlpha16(b,source1));
 }
 for(;x<n;x++){
  Pixel16 in;std::memcpy(&in,src+(size_t)x*8,8);
  float lum=Ops<Pixel16>::luma(in);
  float t=transform(lum,invScale,offset,settings.loop,settings.reflect,settings.invert);
  Pixel16 color=lookup16(lut,t);
  if(in.a!=32768){
   color.a=(uint16_t)(((uint32_t)color.a*in.a+16384u)>>15);
   color.r=(uint16_t)(((uint32_t)color.r*in.a+16384u)>>15);
   color.g=(uint16_t)(((uint32_t)color.g*in.a+16384u)>>15);
   color.b=(uint16_t)(((uint32_t)color.b*in.a+16384u)>>15);
  }
  std::memcpy(dst+(size_t)x*8,&color,8);
 }
}
// Read eight contiguous ARGB32 pixels, deinterleaving their channels
// using fixed SSE shuffles instead of 4x8 random AVX2 gather accesses.
// This reads each cacheline sequentially and benefits Mapping and the
// independent 32bpc color-blend compositor.
struct FloatRGBA8 {__m256 a,r,g,b;};
inline FloatRGBA8 loadRGBAFloat8(const uint8_t* ptr) noexcept {
 const float* p=reinterpret_cast<const float*>(ptr);
 __m128 a0=_mm_loadu_ps(p),r0=_mm_loadu_ps(p+4);
 __m128 g0=_mm_loadu_ps(p+8),b0=_mm_loadu_ps(p+12);
 _MM_TRANSPOSE4_PS(a0,r0,g0,b0);
 __m128 a1=_mm_loadu_ps(p+16),r1=_mm_loadu_ps(p+20);
 __m128 g1=_mm_loadu_ps(p+24),b1=_mm_loadu_ps(p+28);
 _MM_TRANSPOSE4_PS(a1,r1,g1,b1);
 return {
  _mm256_insertf128_ps(_mm256_castps128_ps256(a0),a1,1),
  _mm256_insertf128_ps(_mm256_castps128_ps256(r0),r1,1),
  _mm256_insertf128_ps(_mm256_castps128_ps256(g0),g1,1),
  _mm256_insertf128_ps(_mm256_castps128_ps256(b0),b1,1)
 };
}
// 32bpc Mapping with SIMD-contiguous source reading; no Gather for source.
inline void mapping32(uint8_t* dst,const uint8_t* src,int n,const Settings& settings,
                      const Ramp& ramp,float invScale,float offset,
                      const DirectRamp* direct=nullptr) noexcept {
 const __m256 one=_mm256_set1_ps(1.f),zero=_mm256_setzero_ps();
 int x=0;
 for(;x+8<=n;x+=8){
#if defined(GRADATION_MAPPING_GATHER_SOURCE)
  const float* p=(const float*)src+(size_t)x*4;
  const __m256i indexes=_mm256_setr_epi32(0,4,8,12,16,20,24,28);
  auto load=[&](__m256i idx){return _mm256_i32gather_ps(p,idx,4);};
  const __m256 alphaInput=load(indexes);
  __m256 alpha=_mm256_blendv_ps(zero,alphaInput,
          _mm256_cmp_ps(vabs(alphaInput),_mm256_set1_ps(1.e8f),_CMP_LT_OQ));
  __m256 luminance=_mm256_mul_ps(
      load(_mm256_add_epi32(indexes,_mm256_set1_epi32(1))),
      _mm256_set1_ps(.2126f));
  luminance=_mm256_add_ps(luminance,_mm256_mul_ps(
      load(_mm256_add_epi32(indexes,_mm256_set1_epi32(2))),
      _mm256_set1_ps(.7152f)));
  luminance=_mm256_add_ps(luminance,_mm256_mul_ps(
      load(_mm256_add_epi32(indexes,_mm256_set1_epi32(3))),
      _mm256_set1_ps(.0722f)));
#else
  const FloatRGBA8 pixel=loadRGBAFloat8(src+(size_t)x*16);
  __m256 alpha=pixel.a;
  alpha=_mm256_blendv_ps(zero,alpha,
          _mm256_cmp_ps(vabs(alpha),_mm256_set1_ps(1.e8f),_CMP_LT_OQ));
  __m256 luminance=_mm256_mul_ps(pixel.r,_mm256_set1_ps(.2126f));
  luminance=_mm256_add_ps(luminance,
             _mm256_mul_ps(pixel.g,_mm256_set1_ps(.7152f)));
  luminance=_mm256_add_ps(luminance,
             _mm256_mul_ps(pixel.b,_mm256_set1_ps(.0722f)));
#endif
  luminance=divideNR(luminance,_mm256_max_ps(alpha,_mm256_set1_ps(1e-10f)));
  luminance=_mm256_max_ps(zero,_mm256_min_ps(one,luminance));
  const __m256 t=vtransform(luminance,settings,invScale,offset);
  writeFloat8<true,true>(dst+(size_t)x*16,ramp,t,alpha,direct);
 }
 for(;x<n;x++){
  Pixel32 in;std::memcpy(&in,src+(size_t)x*16,16);
  float luminance=Ops<Pixel32>::luma(in);
  float t=transform(luminance,invScale,offset,settings.loop,settings.reflect,settings.invert);
  Pixel32 out=Ops<Pixel32>::output(
    direct?sampleDirectSrgb(*direct,t):lookup(ramp,t),in);
  std::memcpy(dst+(size_t)x*16,&out,16);
 }
}
// Non-Mapping AVX2 compositor: eight pixels at once for every source alpha.
// Unlike opaqueRow, this handles partial coverage and avoids the former
// per-pixel scalar fallback on antialiased/masked/transparent AE layers.
inline void applyPacked8(uint8_t* dst,const uint8_t* src,__m256i c) noexcept {
 const __m256i input=_mm256_loadu_si256((const __m256i*)src);
 const __m256i mask=_mm256_set1_epi32(255);
 const __m256i alpha=_mm256_and_si256(input,mask);
 // Eliminate alpha multiplications for opaque blocks without a second source
 // scan. This is particularly useful for opaque regions inside matte layers.
 const __m256i opaque=_mm256_cmpeq_epi32(alpha,mask);
 if(_mm256_movemask_epi8(opaque)==-1){
  _mm256_storeu_si256((__m256i*)dst,c);
  return;
 }
 const __m256i repeated=_mm256_or_si256(alpha,_mm256_slli_epi32(alpha,16));
 const __m256i low=_mm256_mullo_epi16(_mm256_and_si256(c,_mm256_set1_epi32(0x00ff00ff)),repeated);
 const __m256i high=_mm256_mullo_epi16(_mm256_and_si256(_mm256_srli_epi32(c,8),
                                         _mm256_set1_epi32(0x00ff00ff)),repeated);
 auto div255=[](__m256i z){
   z=_mm256_add_epi16(z,_mm256_set1_epi16(128));
   z=_mm256_add_epi16(z,_mm256_srli_epi16(z,8));
   return _mm256_and_si256(_mm256_srli_epi16(z,8),_mm256_set1_epi32(0x00ff00ff));
 };
 const __m256i result=_mm256_or_si256(div255(low),_mm256_slli_epi32(div255(high),8));
 _mm256_storeu_si256((__m256i*)dst,result);
}
inline void covered8(uint8_t* dst,const uint8_t* src,const PackedRamp8& lut,__m256 t) noexcept {
 __m256i idx=_mm256_cvtps_epi32(_mm256_mul_ps(t,_mm256_set1_ps(4096.f)));
 idx=_mm256_min_epi32(_mm256_set1_epi32(4096),_mm256_max_epi32(idx,_mm256_setzero_si256()));
 const __m256i c=_mm256_i32gather_epi32((const int*)lut.data(),idx,4);
 applyPacked8(dst,src,c);
}
inline void covered16(uint8_t* dst,const uint8_t* src,const PackedRamp16& lut,__m256 t) noexcept {
 const __m256 coord=_mm256_mul_ps(_mm256_max_ps(_mm256_setzero_ps(),
                    _mm256_min_ps(_mm256_set1_ps(1.f),t)),_mm256_set1_ps(4096.f));
 const __m256i idx=_mm256_cvttps_epi32(_mm256_min_ps(coord,_mm256_set1_ps(4095.f)));
 const __m256i frac=_mm256_cvtps_epi32(_mm256_mul_ps(
                     _mm256_sub_ps(coord,_mm256_cvtepi32_ps(idx)),_mm256_set1_ps(65535.f)));
 const __m256i a=sample4x16(lut,_mm256_castsi256_si128(idx),_mm256_castsi256_si128(frac));
 const __m256i b=sample4x16(lut,_mm256_extracti128_si256(idx,1),_mm256_extracti128_si256(frac,1));
 const __m256i source0=_mm256_loadu_si256((const __m256i*)src);
 const __m256i source1=_mm256_loadu_si256((const __m256i*)(src+32));
 const __m256i mask=_mm256_set1_epi64x(0xffff);
 const __m256i full=_mm256_set1_epi64x(32768);
 const __m256i test0=_mm256_cmpeq_epi64(_mm256_and_si256(source0,mask),full);
 const __m256i test1=_mm256_cmpeq_epi64(_mm256_and_si256(source1,mask),full);
 if(_mm256_movemask_epi8(test0)==-1&&_mm256_movemask_epi8(test1)==-1){
  _mm256_storeu_si256((__m256i*)dst,a);
  _mm256_storeu_si256((__m256i*)(dst+32),b);
  return;
 }
 _mm256_storeu_si256((__m256i*)dst,mulSourceAlpha16(a,source0));
 _mm256_storeu_si256((__m256i*)(dst+32),mulSourceAlpha16(b,source1));
}
inline __m256 alpha8Float(const uint8_t* src) noexcept{
 const float* p=(const float*)src;
 __m128 a0=_mm_loadu_ps(p),a1=_mm_loadu_ps(p+4);
 __m128 a2=_mm_loadu_ps(p+8),a3=_mm_loadu_ps(p+12);
 _MM_TRANSPOSE4_PS(a0,a1,a2,a3);
 __m128 b0=_mm_loadu_ps(p+16),b1=_mm_loadu_ps(p+20);
 __m128 b2=_mm_loadu_ps(p+24),b3=_mm_loadu_ps(p+28);
 _MM_TRANSPOSE4_PS(b0,b1,b2,b3);
 return _mm256_insertf128_ps(_mm256_castps128_ps256(a0),b0,1);
}
template<class P,Shape Mode>
inline void coveredRow(uint8_t* dst,const uint8_t* src,int y,
                       const WritableBuffer& out,const Settings& settings,
                       const Ramp& ramp,const PackedRamp8* packed,
                       const PackedRamp16* packed16,
                       float invLen,float dx,float dy,float startAngle,
                       float invScale,float offset,int first=0,int last=-1,
                       const DirectRamp* direct=nullptr) noexcept {
 static_assert(Mode!=Shape::Mapping);
 if(last<0)last=out.width;
 int x=first;
 for(;x+8<=last;x+=8){
  const __m256 coords=coordinate8<Mode>(x,y,out,settings,invLen,dx,dy,startAngle);
  const __m256 t=vtransform(coords,settings,invScale,offset);
  if constexpr(std::is_same_v<P,Pixel8>){
   covered8(dst+(size_t)x*4,src+(size_t)(x-first)*4,*packed,t);
  }else if constexpr(std::is_same_v<P,Pixel16>){
   covered16(dst+(size_t)x*8,src+(size_t)(x-first)*8,*packed16,t);
  }else{
   const __m256 alpha=alpha8Float(src+(size_t)(x-first)*16);
   writeFloat8<false,true>(dst+(size_t)x*16,ramp,t,alpha,direct);
  }
 }
 constexpr float INV_TAU=1.f/6.28318530717958647692f;
 for(;x<last;x++){
  const float fx=(float)(x+out.originX)-settings.startX;
  const float fy=(float)(y+out.originY)-settings.startY;
  float t=0.f;
  if constexpr(Mode==Shape::Linear){
   t=(fx*dx+fy*dy)*invLen*invLen;
  }else if constexpr(Mode==Shape::Radial){
   t=std::sqrt(fx*fx+fy*fy)*invLen;
  }else{
   t=(fastAtan2(fy,fx)-startAngle)*INV_TAU;t-=std::floor(t);
  }
  t=transform(t,invScale,offset,settings.loop,settings.reflect,settings.invert);
  if constexpr(std::is_same_v<P,Pixel8>){
   Pixel8 input;std::memcpy(&input,src+(size_t)(x-first)*4,4);
   const uint32_t sample=(*packed)[std::clamp((int)(t*4096.f+.5f),0,4096)];
   Pixel8 result{};
   for(int ch=0;ch<4;ch++){
    const unsigned z=(sample>>(ch*8))&255u;
    ((uint8_t*)&result)[ch]=(uint8_t)((z*input.a+127u)/255u);
   }
   std::memcpy(dst+(size_t)x*4,&result,4);
  }else if constexpr(std::is_same_v<P,Pixel16>){
   Pixel16 input;std::memcpy(&input,src+(size_t)(x-first)*8,8);
   Pixel16 result=lookup16(*packed16,t);
   result.a=(uint16_t)(((uint32_t)result.a*input.a+16384u)>>15);
   result.r=(uint16_t)(((uint32_t)result.r*input.a+16384u)>>15);
   result.g=(uint16_t)(((uint32_t)result.g*input.a+16384u)>>15);
   result.b=(uint16_t)(((uint32_t)result.b*input.a+16384u)>>15);
   std::memcpy(dst+(size_t)x*8,&result,8);
  }else{
   Pixel32 input;std::memcpy(&input,src+(size_t)(x-first)*16,16);
   const Pixel32 result=Ops<Pixel32>::output(
     direct?sampleDirectSrgb(*direct,t):lookup(ramp,t),input);
   std::memcpy(dst+(size_t)x*16,&result,16);
  }
 }
}
template<class P,Shape Mode>
inline void opaqueRow(const WritableBuffer& dst,int y,const Settings& s,
 const Ramp& ramp,const PackedRamp8* packed,const PackedRamp16* packed16,
 float invLen,float dx,float dy,float startAngle,float invScale,float offset,
 const DirectRamp* direct=nullptr) noexcept {
 static_assert(Mode!=Shape::Mapping);
 constexpr int K=4096;
 uint8_t* out=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
 const int n=dst.width;
 int x=0;
 for(;x+8<=n;x+=8){
  __m256 t=vtransform(coordinate8<Mode,std::is_same_v<P,Pixel8>>(x,y,dst,s,invLen,dx,dy,startAngle),
                       s,invScale,offset);
  const __m256i idx=_mm256_cvtps_epi32(_mm256_mul_ps(t,_mm256_set1_ps((float)K)));
  if constexpr(std::is_same_v<P,Pixel8>){
   const __m256i result=_mm256_i32gather_epi32((const int*)packed->data(),idx,4);
   _mm256_storeu_si256((__m256i*)(out+(size_t)x*sizeof(P)),result);
  }else if constexpr(std::is_same_v<P,Pixel16>){
   const __m256 value=_mm256_mul_ps(_mm256_max_ps(_mm256_setzero_ps(),
                             _mm256_min_ps(t,_mm256_set1_ps(1.f))),_mm256_set1_ps(4096.f));
   const __m256i baseIndex=_mm256_cvttps_epi32(_mm256_min_ps(value,_mm256_set1_ps(4095.f)));
   const __m256i frac=_mm256_cvtps_epi32(_mm256_mul_ps(
                        _mm256_sub_ps(value,_mm256_cvtepi32_ps(baseIndex)),
                        _mm256_set1_ps(65535.f)));
   const __m256i a=sample4x16(*packed16,_mm256_castsi256_si128(baseIndex),_mm256_castsi256_si128(frac));
   const __m256i b=sample4x16(*packed16,_mm256_extracti128_si256(baseIndex,1),_mm256_extracti128_si256(frac,1));
   _mm256_storeu_si256((__m256i*)(out+(size_t)x*sizeof(P)),a);
   _mm256_storeu_si256((__m256i*)(out+(size_t)(x+4)*sizeof(P)),b);
  }else{
   writeFloat8<false,true>(out+(size_t)x*sizeof(P),ramp,t,_mm256_set1_ps(1.f),direct);
  }
 }
 for(;x<n;x++){
  float vx=(float)(x+dst.originX)-s.startX,vy=(float)(y+dst.originY)-s.startY;
  float t=0;
  if constexpr(Mode==Shape::Linear)t=(vx*dx+vy*dy)*invLen*invLen;
  else if constexpr(Mode==Shape::Radial)t=std::sqrt(vx*vx+vy*vy)*invLen;
  else{
   t=(fastAtan2(vy,vx)-startAngle)*(1.f/6.28318530717958647692f);
   t-=std::floor(t);
  }
  t=transform(t,invScale,offset,s.loop,s.reflect,s.invert);
  if constexpr(std::is_same_v<P,Pixel8>){
   uint32_t v=(*packed)[std::clamp((int)(t*K+.5f),0,K)];
   std::memcpy(out+(size_t)x*sizeof(P),&v,4);
  }else if constexpr(std::is_same_v<P,Pixel16>){
   Pixel16 v=lookup16(*packed16,t);
   std::memcpy(out+(size_t)x*sizeof(P),&v,8);
  }else{
   const Pixel32 v=Ops<Pixel32>::output(
     direct?sampleDirectSrgb(*direct,t):lookup(ramp,t),Pixel32{1,0,0,0});
   std::memcpy(out+(size_t)x*sizeof(P),&v,sizeof(P));
  }
 }
}
} // namespace simd
#endif
