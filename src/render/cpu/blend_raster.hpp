#pragma once
// Independent compositing kernel, source premultiplied ARGB, gradient straight RGBA.
// No allocation and no AE SDK dependence. Shared across 8/16/32bpc.
#include "blend_modes.hpp"
template<class P>inline Color loadSource(const P& p)noexcept;
template<>inline Color loadSource<Pixel8>(const Pixel8& p)noexcept{
 constexpr float s=1.f/255.f;
 return {p.r*s,p.g*s,p.b*s,p.a*s};
}
template<>inline Color loadSource<Pixel16>(const Pixel16& p)noexcept{
 constexpr float s=1.f/32768.f;
 return {p.r*s,p.g*s,p.b*s,p.a*s};
}
template<>inline Color loadSource<Pixel32>(const Pixel32& p)noexcept{
 return {p.r,p.g,p.b,p.a};
}
template<class P>inline P storeComposite(Color c)noexcept;
template<>inline Pixel8 storeComposite<Pixel8>(Color c)noexcept{
 return {u8(c.a),u8(c.r),u8(c.g),u8(c.b)};
}
template<>inline Pixel16 storeComposite<Pixel16>(Color c)noexcept{
 auto q=[](float v){return (uint16_t)(std::clamp(blend::unit(v),0.f,1.f)*32768.f+.5f);};
 return {q(c.a),q(c.r),q(c.g),q(c.b)};
}
template<>inline Pixel32 storeComposite<Pixel32>(Color c)noexcept{
 return {blend::unit(c.a),blend::sane(c.r),blend::sane(c.g),blend::sane(c.b)};
}
#if defined(__AVX2__) && !defined(GRADATION_DISABLE_COMPOSITE_AXIS_CACHE)
// Full-width, high-depth horizontal/vertical Linear. The straight
// gradient is independent of the source pixels, and is exactly identical
// across rows (horizontal) or across SIMD blocks (vertical).
// Stack-bounded 64KiB; no allocation, persistent state or host calls.
template<class P>
bool renderAxisComposite(const Buffer& src,const WritableBuffer& dst,const Settings& s,
 const Ramp& ramp,const blend::Config& cfg,const DirectRamp* direct,
 float len,float dx,float dy,float angle,float invScale,float offset,
 int64_t sx,int64_t sy) noexcept {
 if(dst.width<128||dst.width>4096||dst.width%8||dst.height<16||
    !src.data||sx<0||sy<0||sx+dst.width>src.width||sy+dst.height>src.height||
    (!std::isfinite(s.startX)||!std::isfinite(s.startY)||
     !std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(invScale)||!std::isfinite(offset))||
    (dx!=0.f&&dy!=0.f)||!std::isfinite(len)||
    std::abs(s.startX)>1.e7f||std::abs(s.startY)>1.e7f||
    std::abs(int64_t(dst.originX))>10000000||std::abs(int64_t(dst.originY))>10000000)
  return false;
 const bool vertical=dx==0.f&&dy!=0.f;
 alignas(32) std::array<simd::Channels,512> cache;
 auto sample=[&](int x,int y){
  return simd::sample8<std::is_same_v<P,Pixel16>,std::is_same_v<P,Pixel32>>(ramp,simd::vtransform(simd::coordinate8<Shape::Linear>(
      x,y,dst,s,1.f/len,dx,dy,angle),s,invScale,offset),direct);
 };
 if(!vertical)for(int x=0;x<dst.width;x+=8)cache[(size_t)x/8]=sample(x,0);
 for(int y=0;y<dst.height;++y){
  if(vertical)cache[0]=sample(0,y);
  auto* row=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
  const auto* input=src.data+(std::ptrdiff_t)(y+sy)*src.rowBytes+size_t(sx)*sizeof(P);
  simd::blendRow8<P,Shape::Linear>(row,input,dst,y,s,ramp,cfg,
      1.f/len,dx,dy,angle,invScale,offset,0,dst.width,direct,cache.data(),vertical);
 }
 return true;
}
#endif
#if !defined(__AVX2__)
template<class P>
bool renderScalarAxisComposite(const Buffer& src,const WritableBuffer& dst,
 const Settings& s,const Ramp& ramp,const blend::Config& cfg,const DirectRamp* direct,
 float dx,float dy,float a,float b,float invScale,float offset,int64_t sx,int64_t sy) noexcept {
 if(!src.data||dst.width<128||dst.width>4096||dst.height<16||
    sx<0||sy<0||sx+dst.width>src.width||sy+dst.height>src.height||
    (dx!=0.f&&dy!=0.f)||!std::isfinite(a)||!std::isfinite(b)||
    !std::isfinite(s.startX)||!std::isfinite(s.startY)||
    !std::isfinite(invScale)||!std::isfinite(offset))
  return false;
 // Keep the original scalar rounding order, including signed zero.
 // Reject unusual rows rather than approximating their coordinates.
 const bool vertical=dx==0.f&&dy!=0.f;
 const auto rowBase=[&](int y){
  return b*(float(y+dst.originY)-s.startY)-a*s.startX;
 };
 const float firstBase=rowBase(0);
 const auto coord=[&](int x,float base){return a*float(x+dst.originX)+base;};
 const auto identical=[](float lhs,float rhs){return std::memcmp(&lhs,&rhs,sizeof(float))==0;};
 for(int y=0;y<dst.height;y++){
  const float base=rowBase(y);
  if(!std::isfinite(base))return false;
  if(vertical){
   if(!identical(coord(0,base),coord(dst.width-1,base)))return false;
  }else if(!identical(base,firstBase))return false;
 }
 const auto sample=[&](float t){
  const float u=transform(t,invScale,offset,s.loop,s.reflect,s.invert);
  return direct?sampleDirectSrgb(*direct,u):lookup(ramp,u);
 };
 // 64 KiB maximum stack storage, independent of image height.
 std::array<Color,4096> scan;
 if(!vertical)for(int x=0;x<dst.width;x++)scan[size_t(x)]=sample(coord(x,firstBase));
 for(int y=0;y<dst.height;y++){
  const auto* in=src.data+std::ptrdiff_t(y+sy)*src.rowBytes+size_t(sx)*sizeof(P);
  auto* out=dst.data+std::ptrdiff_t(y)*dst.rowBytes;
  const Color rowColor=vertical?sample(coord(0,rowBase(y))):Color{};
  auto row=[&](auto getColor){
   for(int x=0;x<dst.width;x++){
    P p{};std::memcpy(&p,in+size_t(x)*sizeof(P),sizeof(P));
    const P result=storeComposite<P>(blend::compose(loadSource<P>(p),getColor(x),cfg,
                         x+dst.originX,y+dst.originY));
    std::memcpy(out+size_t(x)*sizeof(P),&result,sizeof(P));
   }
  };
  if(vertical)row([&](int){return rowColor;});
  else row([&](int x){return scan[size_t(x)];});
 }
 return true;
}
#endif
template<class P,Shape Mode,bool EnableScalarAxisCache=true>
void renderComposite(const Buffer& src,const WritableBuffer& dst,const Settings& s,
                     const Ramp& ramp,const blend::Config& blending,
                     const DirectRamp* direct=nullptr)noexcept {
 const float dx=s.endX-s.startX,dy=s.endY-s.startY;
 const float len=std::max(std::hypot(dx,dy),1e-6f);
 const float invScale=100.f/std::max(s.scalePercent,.0001f);
 const float offset=s.offsetPx/len;
 const float a=dx/(len*len),b=dy/(len*len);
 const float startAngle=fastAtan2(dy,dx);
 constexpr float INV_TAU=1.f/6.28318530717958647692f;
 const int64_t shiftX=(int64_t)dst.originX-(int64_t)src.originX;
 const int64_t shiftY=(int64_t)dst.originY-(int64_t)src.originY;
#if !defined(__AVX2__)
 if constexpr(Mode==Shape::Linear&&EnableScalarAxisCache){
  if(renderScalarAxisComposite<P>(src,dst,s,ramp,blending,direct,dx,dy,a,b,
                                  invScale,offset,shiftX,shiftY))return;
 }
#endif
#if defined(__AVX2__) && !defined(GRADATION_DISABLE_COMPOSITE_AXIS_CACHE)
 if constexpr(Mode==Shape::Linear&&!std::is_same_v<P,Pixel8>){
  if(renderAxisComposite<P>(src,dst,s,ramp,blending,direct,len,dx,dy,
       startAngle,invScale,offset,shiftX,shiftY))return;
 }
#endif
 for(int y=0;y<dst.height;y++){
  uint8_t* row=dst.data+(std::ptrdiff_t)y*dst.rowBytes;
  const int64_t sy=(int64_t)y+shiftY;
  const uint8_t* inputRow=(sy>=0&&sy<src.height&&src.data)?
                src.data+(std::ptrdiff_t)sy*src.rowBytes:nullptr;
#if defined(__AVX2__)
  // Every row, including empty/cropped input worlds, is handled with AVX2:
  // [0, first): transparent black source, [first,last): valid source,
  // [last,width): transparent black source. No allocation, no scalar
  // downgrade for large empty tiles or generated alpha outside source ROI.
  int first=0,last=0;
  if(inputRow){
   first=(int)std::clamp<int64_t>(-shiftX,0,dst.width);
   last=(int)std::clamp<int64_t>((int64_t)src.width-shiftX,0,dst.width);
   if(last<first)last=first;
  }
  // Missing source alpha is exactly zero. For Alpha Multiply this makes
  // the entire outside span transparent regardless of Color Blend mode.
  const bool clearOutside=blending.alpha==blend::Mode::Multiply;
  if(first>0){
   if(clearOutside)std::memset(row,0,(size_t)first*sizeof(P));
   else simd::blendRow8<P,Mode>(row,nullptr,dst,y,s,ramp,blending,
        1.f/len,dx,dy,startAngle,invScale,offset,0,first,direct);
  }
  if(last>first)simd::blendRow8<P,Mode>(
     row,inputRow+(size_t)((int64_t)first+shiftX)*sizeof(P),
     dst,y,s,ramp,blending,1.f/len,dx,dy,
     startAngle,invScale,offset,first,last,direct);
  if(last<dst.width){
   if(clearOutside)std::memset(row+(size_t)last*sizeof(P),0,
                              (size_t)(dst.width-last)*sizeof(P));
   else simd::blendRow8<P,Mode>(row,nullptr,dst,y,s,ramp,blending,
        1.f/len,dx,dy,startAngle,invScale,offset,last,dst.width,direct);
  }
  continue;
#endif
  const float fy=(float)(y+dst.originY);
  const float rowLinear=b*(fy-s.startY)-a*s.startX;
  for(int x=0;x<dst.width;x++){
   P sourcePixel{};
   const int64_t sx=(int64_t)x+shiftX;
   if(inputRow&&sx>=0&&sx<src.width)
    std::memcpy(&sourcePixel,inputRow+(size_t)sx*sizeof(P),sizeof(P));
   const Color source=loadSource<P>(sourcePixel);
   const float fx=(float)(x+dst.originX);
   float t=0.f;
   if constexpr(Mode==Shape::Linear)t=a*fx+rowLinear;
   else if constexpr(Mode==Shape::Radial){
    const float X=fx-s.startX,Y=fy-s.startY;
    t=std::sqrt(X*X+Y*Y)/len;
   }else if constexpr(Mode==Shape::Angle){
    t=(fastAtan2(fy-s.startY,fx-s.startX)-startAngle)*INV_TAU;
    t-=std::floor(t);
   }else if constexpr(Mode==Shape::Mapping){
    t=Ops<P>::luma(sourcePixel);
   }
   const float u=transform(t,invScale,offset,s.loop,s.reflect,s.invert);
   const Color gradient=direct?sampleDirectSrgb(*direct,u):lookup(ramp,u);
   const Color composite=blend::compose(source,gradient,blending,x+dst.originX,y+dst.originY);
   const P out=storeComposite<P>(composite);
   std::memcpy(row+(size_t)x*sizeof(P),&out,sizeof(P));
  }
 }
}

