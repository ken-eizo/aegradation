#pragma once
// Fast premultiplied RGBA compositing. Vectorizes 29 scalar alpha modes,
// 29 separable color modes and uses a scalar lane fallback for four
// non-separable HSL-style modes. No image-sized intermediate buffers.
#if defined(__AVX2__)
#include <immintrin.h>
namespace simd {
using Vec=__m256;
using Int=__m256i;
inline Vec zero()noexcept{return _mm256_setzero_ps();}
inline Vec one()noexcept{return _mm256_set1_ps(1.f);}
inline Vec add(Vec a,Vec b)noexcept{return _mm256_add_ps(a,b);}
inline Vec sub(Vec a,Vec b)noexcept{return _mm256_sub_ps(a,b);}
inline Vec mul(Vec a,Vec b)noexcept{return _mm256_mul_ps(a,b);}
inline Vec min(Vec a,Vec b)noexcept{return _mm256_min_ps(a,b);}
inline Vec max(Vec a,Vec b)noexcept{return _mm256_max_ps(a,b);}
inline Vec cmpLE(Vec a,Vec b)noexcept{return _mm256_cmp_ps(a,b,_CMP_LE_OQ);}
inline Vec cmpLT(Vec a,Vec b)noexcept{return _mm256_cmp_ps(a,b,_CMP_LT_OQ);}
inline Vec pick(Vec a,Vec b,Vec mask)noexcept{return _mm256_blendv_ps(a,b,mask);}
inline Vec clean(Vec a)noexcept{
#if defined(GRADATION_LEGACY_SIMD_MASK)
 return pick(zero(),a,_mm256_cmp_ps(vabs(a),_mm256_set1_ps(3.4e38f),_CMP_LE_OQ));
#else
 return _mm256_and_ps(a,_mm256_cmp_ps(vabs(a),_mm256_set1_ps(3.4e38f),_CMP_LE_OQ));
#endif
}
inline Vec u01(Vec a)noexcept{return min(one(),max(zero(),clean(a)));}
inline Vec ratio(Vec num,Vec den)noexcept{
 return mul(num,reciprocalNR(max(den,_mm256_set1_ps(1e-10f))));
}
inline Vec vb(Vec b,Vec s)noexcept{
 return pick(sub(one(),ratio(sub(one(),b),s)),zero(),cmpLE(s,zero()));
}
inline Vec vd(Vec b,Vec s)noexcept{
 return pick(ratio(b,sub(one(),s)),one(),_mm256_cmp_ps(s,one(),_CMP_GE_OQ));
}
inline Vec soft(Vec b,Vec s)noexcept{
 const Vec D=pick(_mm256_sqrt_ps(max(b,zero())),
              mul(add(mul(add(mul(_mm256_set1_ps(16.f),b),
              _mm256_set1_ps(-12.f)),b),_mm256_set1_ps(4.f)),b),
              cmpLE(b,_mm256_set1_ps(.25f)));
 const Vec lower=sub(b,mul(sub(one(),mul(_mm256_set1_ps(2.f),s)),mul(b,sub(one(),b))));
 const Vec upper=add(b,mul(sub(mul(_mm256_set1_ps(2.f),s),one()),sub(D,b)));
 return pick(upper,lower,cmpLE(s,_mm256_set1_ps(.5f)));
}
template<int StaticMode=-1>
inline Vec channelV(Vec b,Vec s,blend::Mode mode)noexcept{
 const Vec two=_mm256_set1_ps(2.f);
 const Vec half=_mm256_set1_ps(.5f);
 switch(StaticMode<0?mode:static_cast<blend::Mode>(StaticMode)){
 case blend::Mode::Replace:case blend::Mode::Normal:
 case blend::Mode::Dissolve:case blend::Mode::DancingDissolve:return s;
 case blend::Mode::Darken:case blend::Mode::DarkerColor:return min(b,s);
 case blend::Mode::Multiply:return mul(b,s);
 case blend::Mode::ColorBurn:return vb(b,s);
 case blend::Mode::ClassicColorBurn:
   return sub(one(),min(one(),ratio(sub(one(),b),max(s,_mm256_set1_ps(1e-8f)))));
 case blend::Mode::LinearBurn:return sub(add(b,s),one());
 case blend::Mode::Add:case blend::Mode::LinearDodge:return add(b,s);
 case blend::Mode::Lighten:case blend::Mode::LighterColor:return max(b,s);
 case blend::Mode::Screen:return sub(add(b,s),mul(b,s));
 case blend::Mode::ColorDodge:return vd(b,s);
 case blend::Mode::ClassicColorDodge:
   return min(one(),ratio(b,max(sub(one(),s),_mm256_set1_ps(1e-8f))));
 case blend::Mode::Overlay:
   return pick(sub(one(),mul(two,mul(sub(one(),b),sub(one(),s)))),
               mul(two,mul(b,s)),cmpLE(b,half));
 case blend::Mode::SoftLight:return soft(b,s);
 case blend::Mode::HardLight:
   return pick(sub(one(),mul(two,mul(sub(one(),b),sub(one(),s)))),
               mul(two,mul(b,s)),cmpLE(s,half));
 case blend::Mode::LinearLight:return sub(add(b,mul(two,s)),one());
 case blend::Mode::VividLight:
   return pick(vd(b,mul(two,sub(s,half))),vb(b,mul(two,s)),cmpLE(s,half));
 case blend::Mode::PinLight:
   return pick(max(b,sub(mul(two,s),one())),min(b,mul(two,s)),cmpLE(s,half));
 case blend::Mode::HardMix:
   return pick(one(),zero(),cmpLT(channelV(b,s,blend::Mode::VividLight),half));
 case blend::Mode::Difference:case blend::Mode::ClassicDifference:return vabs(sub(b,s));
 case blend::Mode::Exclusion:return sub(add(b,s),mul(two,mul(b,s)));
 case blend::Mode::Subtract:return sub(b,s);
 case blend::Mode::Divide:
   return pick(_mm256_div_ps(b,s),zero(),cmpLT(vabs(s),_mm256_set1_ps(1e-8f)));
 default:return s;
 }
}
inline Vec random8(int x,int y,uint32_t seed)noexcept{
 const Int cols=_mm256_setr_epi32(x,x+1,x+2,x+3,x+4,x+5,x+6,x+7);
 const Int rows=_mm256_set1_epi32(y);
 Int v=_mm256_xor_si256(
  _mm256_xor_si256(_mm256_mullo_epi32(cols,_mm256_set1_epi32((int)0x9e3779b9u)),
                   _mm256_mullo_epi32(rows,_mm256_set1_epi32((int)0x85ebca6bu))),
                   _mm256_set1_epi32((int)(seed*0xc2b2ae35u)));
 v=_mm256_xor_si256(v,_mm256_srli_epi32(v,16));
 v=_mm256_mullo_epi32(v,_mm256_set1_epi32(0x7feb352d));
 v=_mm256_xor_si256(v,_mm256_srli_epi32(v,15));
 v=_mm256_mullo_epi32(v,_mm256_set1_epi32((int)0x846ca68bu));
 v=_mm256_xor_si256(v,_mm256_srli_epi32(v,16));
 return mul(_mm256_cvtepi32_ps(_mm256_srli_epi32(v,8)),_mm256_set1_ps(1.f/16777216.f));
}
struct Channels {
 Vec a,r,g,b;
};
inline Channels source8(const uint8_t* ptr)noexcept {
 const Int p=_mm256_loadu_si256((const Int*)ptr);
 const Int mask=_mm256_set1_epi32(255);
 const Vec n=_mm256_set1_ps(1.f/255.f);
 return {
   mul(_mm256_cvtepi32_ps(_mm256_and_si256(p,mask)),n),
   mul(_mm256_cvtepi32_ps(_mm256_and_si256(_mm256_srli_epi32(p,8),mask)),n),
   mul(_mm256_cvtepi32_ps(_mm256_and_si256(_mm256_srli_epi32(p,16),mask)),n),
   mul(_mm256_cvtepi32_ps(_mm256_srli_epi32(p,24)),n)
 };
}
inline Channels source16(const uint8_t* ptr)noexcept{
 Int ar{},gb{};
#if defined(GRADATION_SOURCE16_GATHER_BASELINE)
 // Preserved only for controlled A/B benchmarking.
 const Int index=_mm256_setr_epi32(0,2,4,6,8,10,12,14);
 ar=_mm256_i32gather_epi32((const int*)ptr,index,4);
 gb=_mm256_i32gather_epi32((const int*)(ptr+4),index,4);
#else
 // 8 adjacent AE ARGB16 pixels occupy exactly two 32-byte cacheline spans.
 // Load sequentially, then move 32-bit AR / GB pairs into contiguous lanes.
 // Unlike two separate AVX2 gathers, this needs only two linear loads.
 const Int first=_mm256_loadu_si256((const Int*)ptr);
 const Int second=_mm256_loadu_si256((const Int*)(ptr+32));
 const Int idx=_mm256_setr_epi32(0,2,4,6,1,3,5,7);
 const Int p0=_mm256_permutevar8x32_epi32(first,idx);
 const Int p1=_mm256_permutevar8x32_epi32(second,idx);
 ar=_mm256_permute2x128_si256(p0,p1,0x20);
 gb=_mm256_permute2x128_si256(p0,p1,0x31);
#endif
 const Int mask=_mm256_set1_epi32(65535);
 const Vec n=_mm256_set1_ps(1.f/32768.f);
 return {
  mul(_mm256_cvtepi32_ps(_mm256_and_si256(ar,mask)),n),
  mul(_mm256_cvtepi32_ps(_mm256_srli_epi32(ar,16)),n),
  mul(_mm256_cvtepi32_ps(_mm256_and_si256(gb,mask)),n),
  mul(_mm256_cvtepi32_ps(_mm256_srli_epi32(gb,16)),n)
 };
}
inline Channels source32(const uint8_t* ptr)noexcept{
 const FloatRGBA8 v=loadRGBAFloat8(ptr);
 return {v.a,v.r,v.g,v.b};
}
template<bool SelectCoefficients=false,bool TrustedT=false>
inline Channels sample8(const Ramp& ramp,Vec t,
                           const DirectRamp* direct=nullptr)noexcept{
 if(direct){
  const Vec u=TrustedT?t:u01(t);
  const bool segmented=direct->count==3;
  const Vec split=_mm256_set1_ps(direct->middlePosition);
  const Vec mask=_mm256_cmp_ps(u,split,_CMP_LT_OQ);
#if defined(GRADATION_EXPERIMENT_SEGMENTED_BLEND)
  // Smooth spatial ramps usually have eight consecutive lanes in the
  // same segment. Skip the unused half; retain the identical arithmetic
  // and saturation for the chosen segment and the mixed-block fallback.
  if(segmented){
   const int lanes=_mm256_movemask_ps(mask);
   if(lanes==0||lanes==255){
    const bool left=lanes==255;
    const Color& start=left?direct->first:direct->middle;
    const Color& end=left?direct->middle:direct->last;
    const Vec pos=left?
     u01(mul(u,_mm256_set1_ps(1.f/direct->middlePosition))):
     u01(mul(sub(u,split),_mm256_set1_ps(1.f/(1.f-direct->middlePosition))));
    auto lerp=[&](float a,float b){return add(_mm256_set1_ps(a),mul(pos,_mm256_set1_ps(b-a)));};
    return {u01(lerp(start.a,end.a)),clean(lerp(start.r,end.r)),
            clean(lerp(start.g,end.g)),clean(lerp(start.b,end.b))};
   }
  }
#endif
  const Vec before=segmented?
    u01(mul(u,_mm256_set1_ps(1.f/direct->middlePosition))):u;
  const Vec after=segmented?
    u01(mul(sub(u,split),_mm256_set1_ps(1.f/(1.f-direct->middlePosition)))):u;
  const Vec part=pick(after,before,mask);
  auto lerp=[&](float a,float b,float mid){
   if(!segmented)return add(_mm256_set1_ps(a),mul(u,_mm256_set1_ps(b-a)));
   if constexpr(SelectCoefficients){
    // Select the affine coefficients before doing arithmetic: identical
    // per-lane operands and rounding. The 16bpc graph benefits; the
    // 32bpc generator keeps its faster original instruction schedule.
    const Vec base=pick(_mm256_set1_ps(mid),_mm256_set1_ps(a),mask);
    const Vec slope=pick(_mm256_set1_ps(b-mid),_mm256_set1_ps(mid-a),mask);
    return add(base,mul(part,slope));
   }else{
    const Vec left=add(_mm256_set1_ps(a),mul(before,_mm256_set1_ps(mid-a)));
    const Vec right=add(_mm256_set1_ps(mid),mul(after,_mm256_set1_ps(b-mid)));
    return pick(right,left,mask);
   }
  };
  return {u01(lerp(direct->first.a,direct->last.a,direct->middle.a)),
          clean(lerp(direct->first.r,direct->last.r,direct->middle.r)),
          clean(lerp(direct->first.g,direct->last.g,direct->middle.g)),
          clean(lerp(direct->first.b,direct->last.b,direct->middle.b))};
 }
 Vec coord=mul(TrustedT?t:u01(t),_mm256_set1_ps(4096.f));
 const Int ix=_mm256_cvttps_epi32(min(coord,_mm256_set1_ps(4095.f)));
 const Vec fraction=sub(coord,_mm256_cvtepi32_ps(ix));
#if defined(GRADATION_PACKED_BLEND_LUT)
 // One adjacent RGBA pair per pixel, rather than eight separate
 // channel gathers. Preserve exact subtraction/multiply/add ordering.
 alignas(32) int indexes[8];
 alignas(32) float fractions[8];
 _mm256_store_si256((Int*)indexes,ix);
 _mm256_store_ps(fractions,fraction);
 const auto pixel=[&](int j){
  const auto pair=_mm256_loadu_ps((const float*)&ramp[(size_t)indexes[j]]);
  const auto left=_mm256_castps256_ps128(pair);
  const auto right=_mm256_extractf128_ps(pair,1);
  return _mm_add_ps(left,_mm_mul_ps(_mm_set1_ps(fractions[j]),_mm_sub_ps(right,left)));
 };
 auto r0=pixel(0),g0=pixel(1),b0=pixel(2),a0=pixel(3);
 auto r1=pixel(4),g1=pixel(5),b1=pixel(6),a1=pixel(7);
 _MM_TRANSPOSE4_PS(r0,g0,b0,a0);
 _MM_TRANSPOSE4_PS(r1,g1,b1,a1);
 auto join=[](__m128 x,__m128 y){return _mm256_insertf128_ps(_mm256_castps128_ps256(x),y,1);};
 return {u01(join(a0,a1)),clean(join(r0,r1)),clean(join(g0,g1)),clean(join(b0,b1))};
#else
 const Int offsets=_mm256_slli_epi32(ix,2);
 const float* pixels=(const float*)ramp.data();
 auto chan=[&](int channel)->Vec{
  const Int j=_mm256_add_epi32(offsets,_mm256_set1_epi32(channel));
  const Vec left=_mm256_i32gather_ps(pixels,j,4);
  const Vec right=_mm256_i32gather_ps(pixels,_mm256_add_epi32(j,_mm256_set1_epi32(4)),4);
  return add(left,mul(fraction,sub(right,left)));
 };
 return {u01(chan(3)),clean(chan(0)),clean(chan(1)),clean(chan(2))};
#endif
}
template<int StaticAlpha=-1>
inline Vec alpha8(Channels src,Channels fg,blend::Config cfg,int x,int y)noexcept{
 const Vec a=u01(src.a),g=u01(fg.a);
 const auto alphaMode=StaticAlpha<0?cfg.alpha:static_cast<blend::Mode>(StaticAlpha);
 if(alphaMode==blend::Mode::Replace)return g;
 if(alphaMode==blend::Mode::Normal)return add(g,mul(a,sub(one(),g)));
 if(alphaMode==blend::Mode::Dissolve||
    alphaMode==blend::Mode::DancingDissolve){
  const uint32_t seed=alphaMode==blend::Mode::Dissolve?0:cfg.frameSeed+1u;
  return pick(a,one(),cmpLT(random8(x,y,seed),g));
 }
 return u01(channelV<StaticAlpha>(a,g,alphaMode));
}
struct RGBv {Vec r,g,b;};
inline Vec vlum(RGBv c)noexcept{
 return add(add(mul(c.r,_mm256_set1_ps(.3f)),mul(c.g,_mm256_set1_ps(.59f))),
                mul(c.b,_mm256_set1_ps(.11f)));
}
inline Vec vsat(RGBv c)noexcept{
 return sub(max(c.r,max(c.g,c.b)),min(c.r,min(c.g,c.b)));
}
inline RGBv vsetSat(RGBv c,Vec desired)noexcept{
 const Vec lo=min(c.r,min(c.g,c.b));
 const Vec hi=max(c.r,max(c.g,c.b));
 const Vec extent=sub(hi,lo);
 const Vec factor=pick(zero(),ratio(desired,extent),
                  _mm256_cmp_ps(extent,zero(),_CMP_GT_OQ));
 return {mul(sub(c.r,lo),factor),mul(sub(c.g,lo),factor),
         mul(sub(c.b,lo),factor)};
}
inline RGBv vclipColor(RGBv c)noexcept{
 const Vec l=vlum(c);
 const Vec n=min(c.r,min(c.g,c.b));
 const Vec hi=max(c.r,max(c.g,c.b));
#if !defined(GRADATION_DISABLE_CLIP_EARLY)
 if(_mm256_movemask_ps(_mm256_or_ps(cmpLT(n,zero()),
                      _mm256_cmp_ps(hi,one(),_CMP_GT_OQ)))==0)
  return c;
#endif
 const Vec dLow=sub(l,n),dHigh=sub(hi,l);
 const Vec factorLow=pick(zero(),ratio(l,dLow),
                          _mm256_cmp_ps(vabs(dLow),_mm256_set1_ps(1e-10f),_CMP_GT_OQ));
 const Vec factorHigh=pick(zero(),ratio(sub(one(),l),dHigh),
                           _mm256_cmp_ps(vabs(dHigh),_mm256_set1_ps(1e-10f),_CMP_GT_OQ));
 const Vec lowMask=cmpLT(n,zero());
 const Vec highMask=_mm256_cmp_ps(hi,one(),_CMP_GT_OQ);
 auto adjust=[&](Vec v)->Vec{
  v=pick(v,add(l,mul(sub(v,l),factorLow)),lowMask);
  return pick(v,add(l,mul(sub(v,l),factorHigh)),highMask);
 };
 return {adjust(c.r),adjust(c.g),adjust(c.b)};
}
inline RGBv vsetLum(RGBv c,Vec target)noexcept{
 const Vec offset=sub(target,vlum(c));
 return vclipColor({add(c.r,offset),add(c.g,offset),add(c.b,offset)});
}
template<int StaticColor=-1,int StaticAlpha=-1>
inline Channels composite8(Channels src,Channels fg,blend::Config cfg,int x,int y)noexcept{
 Vec as=u01(src.a),ag=u01(fg.a);
 const auto colorMode=StaticColor<0?cfg.color:static_cast<blend::Mode>(StaticColor);
 const Vec alphaOut=alpha8<StaticAlpha>(src,fg,cfg,x,y);
 if(colorMode==blend::Mode::Replace){
  return {alphaOut,mul(fg.r,alphaOut),mul(fg.g,alphaOut),mul(fg.b,alphaOut)};
 }
 Vec coverage=ag;
 if(colorMode==blend::Mode::Dissolve||colorMode==blend::Mode::DancingDissolve){
  const uint32_t seed=colorMode==blend::Mode::Dissolve?0:cfg.frameSeed+1u;
  coverage=pick(zero(),one(),cmpLT(random8(x,y,seed),ag));
 }
 const Vec sourcePresence=_mm256_cmp_ps(as,_mm256_set1_ps(1e-10f),_CMP_GT_OQ);
 const Vec sr=_mm256_and_ps(clean(src.r),sourcePresence);
 const Vec sg=_mm256_and_ps(clean(src.g),sourcePresence);
 const Vec sb=_mm256_and_ps(clean(src.b),sourcePresence);
 const Vec n=add(coverage,mul(as,sub(one(),coverage)));
 const Vec present=_mm256_cmp_ps(n,_mm256_set1_ps(1e-10f),_CMP_GT_OQ);
 // For normal alpha and ordinary color coverage, alphaOut and n are
 // identical by construction. n/n is exactly 1, not an approximate
 // reciprocal; retain the legacy zero-normal-alpha threshold.
#if defined(GRADATION_LEGACY_NORMAL_FACTOR)
 const Vec factor=pick(zero(),ratio(alphaOut,n),present);
#else
 const bool identityFactor=(StaticAlpha<0?cfg.alpha:static_cast<blend::Mode>(StaticAlpha))==blend::Mode::Normal&&
     colorMode!=blend::Mode::Dissolve&&colorMode!=blend::Mode::DancingDissolve;
 const Vec factor=identityFactor?pick(zero(),one(),present):
                      pick(zero(),ratio(alphaOut,n),present);
#endif
 if(colorMode==blend::Mode::Normal||
    colorMode==blend::Mode::Dissolve||
    colorMode==blend::Mode::DancingDissolve){
  const Vec sgW=sub(one(),coverage);
  return {alphaOut,mul(add(mul(sr,sgW),mul(fg.r,coverage)),factor),
                  mul(add(mul(sg,sgW),mul(fg.g,coverage)),factor),
                  mul(add(mul(sb,sgW),mul(fg.b,coverage)),factor)};
 }
 const Vec invSrc=ratio(one(),as);
 const Vec br=mul(sr,invSrc),bg=mul(sg,invSrc),bb=mul(sb,invSrc);
 Vec blendR{},blendG{},blendB{};
 if(colorMode==blend::Mode::Hue||colorMode==blend::Mode::Saturation||
    colorMode==blend::Mode::Color||colorMode==blend::Mode::Luminosity){
  const RGBv background{br,bg,bb},foreground{fg.r,fg.g,fg.b};
  RGBv result{};
  if(colorMode==blend::Mode::Hue)
   result=vsetLum(vsetSat(foreground,vsat(background)),vlum(background));
  else if(colorMode==blend::Mode::Saturation)
   result=vsetLum(vsetSat(background,vsat(foreground)),vlum(background));
  else if(colorMode==blend::Mode::Color)
   result=vsetLum(foreground,vlum(background));
  else result=vsetLum(background,vlum(foreground));
  blendR=result.r;blendG=result.g;blendB=result.b;
 }else if(colorMode==blend::Mode::DarkerColor||colorMode==blend::Mode::LighterColor){
  const Vec lumBg=add(add(mul(br,_mm256_set1_ps(.3f)),mul(bg,_mm256_set1_ps(.59f))),
                        mul(bb,_mm256_set1_ps(.11f)));
  const Vec lumFg=add(add(mul(fg.r,_mm256_set1_ps(.3f)),mul(fg.g,_mm256_set1_ps(.59f))),
                        mul(fg.b,_mm256_set1_ps(.11f)));
  const Vec chooseSrc=colorMode==blend::Mode::DarkerColor?
                        cmpLE(lumBg,lumFg):_mm256_cmp_ps(lumBg,lumFg,_CMP_GE_OQ);
  blendR=pick(fg.r,br,chooseSrc);blendG=pick(fg.g,bg,chooseSrc);
  blendB=pick(fg.b,bb,chooseSrc);
 }else{
  blendR=channelV<StaticColor>(br,fg.r,colorMode);
  blendG=channelV<StaticColor>(bg,fg.g,colorMode);
  blendB=channelV<StaticColor>(bb,fg.b,colorMode);
 }
 const Vec sW=sub(one(),coverage);
 const Vec foregroundW=mul(coverage,sub(one(),as));
 const Vec overlap=mul(coverage,as);
 auto channel=[&](Vec source,Vec fgColor,Vec blended){
  return mul(add(add(mul(source,sW),mul(fgColor,foregroundW)),
                 mul(blended,overlap)),factor);
 };
 return {alphaOut,channel(sr,fg.r,blendR),channel(sg,fg.g,blendG),
                  channel(sb,fg.b,blendB)};
}
// Common UI combination: Color Normal + Alpha Multiply.
// Hoist mode selection out of per-pixel math. This is a specialized
// algebraic expansion of composite8(), including the exact zero-alpha
// and premultiplied-source rules. No dynamic dispatch, allocations,
// normal-alpha/special blending case switches, or scalar pixel fallback.
inline Channels compositeNormalMultiply8(Channels src,Channels fg) noexcept {
 const Vec as=u01(src.a),ag=u01(fg.a);
 const Vec ao=u01(mul(as,ag));
 const Vec normalA=add(ag,mul(as,sub(one(),ag)));
 const Vec factor=pick(zero(),ratio(ao,normalA),
   _mm256_cmp_ps(normalA,_mm256_set1_ps(1e-10f),_CMP_GT_OQ));
 const Vec presence=_mm256_cmp_ps(as,_mm256_set1_ps(1e-10f),_CMP_GT_OQ);
 const Vec sourceWeight=sub(one(),ag);
 auto mixChannel=[&](Vec base,Vec top){
  const Vec bg=_mm256_and_ps(clean(base),presence);
  return mul(add(mul(bg,sourceWeight),mul(top,ag)),factor);
 };
 return {ao,mixChannel(src.r,fg.r),mixChannel(src.g,fg.g),
            mixChannel(src.b,fg.b)};
}
inline void store8(uint8_t* dst,Channels c)noexcept{
 const Vec one255=_mm256_set1_ps(255.f),half=_mm256_set1_ps(.5f);
 auto u8=[&](Vec v)->Int{
  return _mm256_cvttps_epi32(add(mul(u01(v),one255),half));
 };
 const Int a=u8(c.a),r=u8(c.r),g=u8(c.g),b=u8(c.b);
 const Int ab=_mm256_or_si256(a,_mm256_slli_epi32(r,8));
 const Int cd=_mm256_or_si256(_mm256_slli_epi32(g,16),_mm256_slli_epi32(b,24));
 const Int o=_mm256_or_si256(ab,cd);
 _mm256_storeu_si256((Int*)dst,o);
}
inline void store16(uint8_t* dst,Channels c)noexcept{
 const Vec scale=_mm256_set1_ps(32768.f),half=_mm256_set1_ps(.5f);
 auto u16=[&](Vec v)->Int{
  return _mm256_cvttps_epi32(add(mul(u01(v),scale),half));
 };
 const Int a=u16(c.a),r=u16(c.r),g=u16(c.g),b=u16(c.b);
 const Int ar=_mm256_or_si256(a,_mm256_slli_epi32(r,16));
 const Int gb=_mm256_or_si256(g,_mm256_slli_epi32(b,16));
 const Int l=_mm256_unpacklo_epi32(ar,gb),h=_mm256_unpackhi_epi32(ar,gb);
 const Int out0=_mm256_permute2x128_si256(l,h,0x20);
 const Int out1=_mm256_permute2x128_si256(l,h,0x31);
 _mm256_storeu_si256((Int*)dst,out0);
 _mm256_storeu_si256((Int*)(dst+32),out1);
}
inline void store32(uint8_t* dst,Channels c)noexcept{
 const Vec a=u01(c.a),r=clean(c.r),g=clean(c.g),b=clean(c.b);
#if defined(GRADATION_F32_OLD_TRANSPOSE)
 __m128 a0=_mm256_castps256_ps128(a),r0=_mm256_castps256_ps128(r);
 __m128 g0=_mm256_castps256_ps128(g),b0=_mm256_castps256_ps128(b);
 _MM_TRANSPOSE4_PS(a0,r0,g0,b0);
 _mm_storeu_ps((float*)(dst+0),a0);
 _mm_storeu_ps((float*)(dst+16),r0);
 _mm_storeu_ps((float*)(dst+32),g0);
 _mm_storeu_ps((float*)(dst+48),b0);
 __m128 a1=_mm256_extractf128_ps(a,1),r1=_mm256_extractf128_ps(r,1);
 __m128 g1=_mm256_extractf128_ps(g,1),b1=_mm256_extractf128_ps(b,1);
 _MM_TRANSPOSE4_PS(a1,r1,g1,b1);
 _mm_storeu_ps((float*)(dst+64),a1);
 _mm_storeu_ps((float*)(dst+80),r1);
 _mm_storeu_ps((float*)(dst+96),g1);
 _mm_storeu_ps((float*)(dst+112),b1);
#else
 storeFloatARGB8(dst,a,r,g,b);
#endif
}
// Native AE ARGB32: two pixels per YMM register. The foreground
// interpolator already has eight SoA pixels, so transpose ONLY that
// operand; the source and destination never leave their interleaved form.
inline void store32NormalAoS(uint8_t* dst,const uint8_t* src,Channels fg) noexcept {
 const Vec a=u01(fg.a),r=mul(fg.r,a),g=mul(fg.g,a),b=mul(fg.b,a);
 const Vec ar0=_mm256_unpacklo_ps(a,r),ar1=_mm256_unpackhi_ps(a,r);
 const Vec gb0=_mm256_unpacklo_ps(g,b),gb1=_mm256_unpackhi_ps(g,b);
 const Vec p04=_mm256_shuffle_ps(ar0,gb0,0x44),p15=_mm256_shuffle_ps(ar0,gb0,0xEE);
 const Vec p26=_mm256_shuffle_ps(ar1,gb1,0x44),p37=_mm256_shuffle_ps(ar1,gb1,0xEE);
 const auto pair=[&](int off,Vec top){
  const Vec input=src?_mm256_loadu_ps((const float*)(src+off)):zero();
  const Vec as=u01(_mm256_shuffle_ps(input,input,0x00));
  const Vec ag=_mm256_shuffle_ps(top,top,0x00);
  const Vec w=sub(one(),ag);
  const Vec source=_mm256_blend_ps(_mm256_and_ps(clean(input),
      _mm256_cmp_ps(as,_mm256_set1_ps(1.e-10f),_CMP_GT_OQ)),as,0x11);
  const Vec normalA=add(ag,mul(as,w));
  const Vec factor=_mm256_and_ps(one(),_mm256_cmp_ps(normalA,_mm256_set1_ps(1.e-10f),_CMP_GT_OQ));
  const Vec result=clean(mul(add(mul(source,w),top),factor));
  _mm256_storeu_ps((float*)(dst+off),_mm256_blend_ps(result,u01(normalA),0x11));
 };
 pair(0,_mm256_permute2f128_ps(p04,p15,0x20));
 pair(32,_mm256_permute2f128_ps(p26,p37,0x20));
 pair(64,_mm256_permute2f128_ps(p04,p15,0x31));
 pair(96,_mm256_permute2f128_ps(p26,p37,0x31));
}
template<class P,Shape Mode,int StaticColor=-1,int StaticAlpha=-1>
inline void blendRow8Impl(uint8_t* dst,const uint8_t* src,const WritableBuffer& out,
 int y,const Settings& settings,const Ramp& ramp,const blend::Config& config,
 float invLen,float dx,float dy,float startAngle,float invScale,float offset,
 int first=0,int last=-1,
 const DirectRamp* direct=nullptr,const Channels* cached=nullptr,
 bool repeat=false)noexcept{
 const auto colorMode=StaticColor<0?config.color:static_cast<blend::Mode>(StaticColor);
 const auto alphaMode=StaticAlpha<0?config.alpha:static_cast<blend::Mode>(StaticAlpha);
 if(last<0)last=out.width;
#if defined(GRADATION_EXPERIMENT_NORMAL_AOS)
 const bool aosNormal=std::is_same_v<P,Pixel32>&&Mode!=Shape::Mapping&&
    colorMode==blend::Mode::Normal&&alphaMode==blend::Mode::Normal;
#else
 constexpr bool aosNormal=false;
#endif
 int x=first;
 for(;x+8<=last;x+=8){
  Channels s{};
  if(src&&!aosNormal){
   const uint8_t* input=src+(size_t)(x-first)*sizeof(P);
   if constexpr(std::is_same_v<P,Pixel8>)s=source8(input);
   else if constexpr(std::is_same_v<P,Pixel16>)s=source16(input);
   else s=source32(input);
  }
  Channels gradient;
  if(cached)gradient=cached[repeat?0:(x-first)/8];
  else{
  Vec t{};
  if constexpr(Mode==Shape::Mapping){
   const Vec luminance=add(add(mul(s.r,_mm256_set1_ps(.2126f)),
                                  mul(s.g,_mm256_set1_ps(.7152f))),
                                  mul(s.b,_mm256_set1_ps(.0722f)));
   t=u01(pick(zero(),ratio(luminance,s.a),
          _mm256_cmp_ps(s.a,_mm256_set1_ps(1e-10f),_CMP_GT_OQ)));
  }else t=coordinate8<Mode>(x,y,out,settings,invLen,dx,dy,startAngle);
  t=vtransform(t,settings,invScale,offset);
  // Byte-depth directly interpolated stops are eligible only for the
  // continuous Normal compositing operation with standard alpha modes.
  // HardMix and other thresholded blend modes retain the established LUT.
  const bool directForU8=colorMode==blend::Mode::Normal &&
    (alphaMode==blend::Mode::Multiply ||
     alphaMode==blend::Mode::Normal ||
     alphaMode==blend::Mode::Replace);
  gradient=sample8<std::is_same_v<P,Pixel16>,std::is_same_v<P,Pixel32>>(ramp,t,
    (!std::is_same_v<P,Pixel8>||directForU8)?direct:nullptr);
  }
  if(aosNormal){
   store32NormalAoS(dst+(size_t)x*sizeof(P),src?src+(size_t)(x-first)*sizeof(P):nullptr,gradient);
   continue;
  }
#ifndef GRADATION_DISABLE_FAST_NORMAL_MULTIPLY
  const Channels result=(colorMode==blend::Mode::Normal&&
                         alphaMode==blend::Mode::Multiply)?
      compositeNormalMultiply8(s,gradient):
      composite8<StaticColor,StaticAlpha>(s,gradient,config,x+out.originX,y+out.originY);
#else
  const Channels result=composite8<StaticColor,StaticAlpha>(s,gradient,config,
                                  x+out.originX,y+out.originY);
#endif
  if constexpr(std::is_same_v<P,Pixel8>)store8(dst+(size_t)x*sizeof(P),result);
  else if constexpr(std::is_same_v<P,Pixel16>)store16(dst+(size_t)x*sizeof(P),result);
  else store32(dst+(size_t)x*sizeof(P),result);
 }
 // Odd image widths: exact scalar reference tail.
 for(;x<last;x++){
  P sourcePixel{};
  if(src)std::memcpy(&sourcePixel,src+(size_t)(x-first)*sizeof(P),sizeof(P));
  const Color source=loadSource<P>(sourcePixel);
  const float fx=(float)(x+out.originX);
  const float fy=(float)(y+out.originY);
  const float sx=fx-settings.startX,sy=fy-settings.startY;
  float t=0;
  if constexpr(Mode==Shape::Linear)
   t=(sx*dx+sy*dy)*invLen*invLen;
  else if constexpr(Mode==Shape::Radial)
   t=std::sqrt(sx*sx+sy*sy)*invLen;
  else if constexpr(Mode==Shape::Angle){
   t=(fastAtan2(sy,sx)-startAngle)*(1.f/6.28318530717958647692f);
   t-=std::floor(t);
  }else
   t=Ops<P>::luma(sourcePixel);
  t=transform(t,invScale,offset,settings.loop,settings.reflect,settings.invert);
  // Odd image widths use the same exact direct sRGB interpolation as
  // full eight-pixel batches. Otherwise a lazily unprepared LUT would
  // produce invalid tail pixels on 32bpc F32 effects.
  constexpr bool byteDepth=std::is_same_v<P,Pixel8>;
  const bool byteDirect=colorMode==blend::Mode::Normal&&
    (alphaMode==blend::Mode::Multiply||
     alphaMode==blend::Mode::Normal||
     alphaMode==blend::Mode::Replace);
  const Color grad=(direct&&(!byteDepth||byteDirect))?
                    sampleDirectSrgb(*direct,t):lookup(ramp,t);
  const Color mixed=blend::compose(source,grad,config,x+out.originX,y+out.originY);
  const P output=storeComposite<P>(mixed);
  std::memcpy(dst+(size_t)x*sizeof(P),&output,sizeof(P));
 }
}
// Compile common blend graphs ahead of time. Choose once per scanline,
// not once per SIMD block/channel. Generic graph remains the fallback.
template<class P,Shape Mode>
inline void blendRow8(uint8_t* dst,const uint8_t* src,const WritableBuffer& out,
 int y,const Settings& settings,const Ramp& ramp,const blend::Config& config,
 float invLen,float dx,float dy,float startAngle,float invScale,float offset,
 int first=0,int last=-1,const DirectRamp* direct=nullptr,
 const Channels* cached=nullptr,bool repeat=false)noexcept{
#if !defined(GRADATION_GENERIC_BLEND_GRAPH)
 using B=blend::Mode;
 #define GRADATION_GRAPH(C,A) \
 if(config.color==B::C&&config.alpha==B::A){ \
  return blendRow8Impl<P,Mode,int(B::C),int(B::A)>(dst,src,out,y,settings,ramp,config,invLen,dx,dy,startAngle,invScale,offset,first,last,direct,cached,repeat); \
 }
 GRADATION_GRAPH(Normal,Normal)
 GRADATION_GRAPH(Screen,Multiply)
 GRADATION_GRAPH(SoftLight,Multiply)
 GRADATION_GRAPH(Hue,Normal)
 #undef GRADATION_GRAPH
#endif
 return blendRow8Impl<P,Mode>(dst,src,out,y,settings,ramp,config,invLen,dx,dy,startAngle,invScale,offset,first,last,direct,cached,repeat);
}
} // namespace simd
#endif
