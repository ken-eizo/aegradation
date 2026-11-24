#pragma once
// AE-style blend mathematics for gradient-on-source compositing.
// Canonical channels are straight RGB; outputs are premultiplied ARGB.
// Adobe 32bpc permits negative / >1 RGB, while alpha stays normalized.
// Legacy "Classic" variants are approximations, not host bitwise replicas.
#include "../../core/gradient_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
namespace gradation::blend {
enum class Mode : uint8_t {
 Replace, Normal, Dissolve, DancingDissolve,
 Darken, Multiply, ColorBurn, ClassicColorBurn, LinearBurn, DarkerColor,
 Add, Lighten, Screen, ColorDodge, ClassicColorDodge, LinearDodge, LighterColor,
 Overlay, SoftLight, HardLight, LinearLight, VividLight, PinLight, HardMix,
 Difference, ClassicDifference, Exclusion, Subtract, Divide,
 Hue, Saturation, Color, Luminosity
};
inline constexpr int ColorChoices=33;
inline constexpr int AlphaChoices=29; // exclude nonseparable Hue / Saturation / Color / Luminosity
inline constexpr char ColorOptions[]=
 "Replace|Normal|Dissolve|Dancing Dissolve|Darken|Multiply|Color Burn|Classic Color Burn|"
 "Linear Burn|Darker Color|Add|Lighten|Screen|Color Dodge|Classic Color Dodge|"
 "Linear Dodge|Lighter Color|Overlay|Soft Light|Hard Light|Linear Light|"
 "Vivid Light|Pin Light|Hard Mix|Difference|Classic Difference|Exclusion|Subtract|"
 "Divide|Hue|Saturation|Color|Luminosity";
inline constexpr char AlphaOptions[]=
 "Replace|Normal|Dissolve|Dancing Dissolve|Darken|Multiply|Color Burn|Classic Color Burn|"
 "Linear Burn|Darker Color|Add|Lighten|Screen|Color Dodge|Classic Color Dodge|"
 "Linear Dodge|Lighter Color|Overlay|Soft Light|Hard Light|Linear Light|"
 "Vivid Light|Pin Light|Hard Mix|Difference|Classic Difference|Exclusion|Subtract|Divide";
struct Config {
 // C++ default preserves the previous raster kernel's test/SDK behavior.
 // The AE user-visible Color Blend popup defaults to Normal explicitly.
 Mode color=Mode::Replace;
 Mode alpha=Mode::Multiply;
 uint32_t frameSeed=0;
 bool legacyFast()const noexcept {
  return color==Mode::Replace&&alpha==Mode::Multiply;
 }
};
inline float unit(float v) noexcept {
 return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;
}
inline float sane(float v) noexcept {
 return std::isfinite(v)?v:0.f;
}
inline uint32_t hash(int x,int y,uint32_t seed) noexcept {
 uint32_t v=(uint32_t)x*0x9e3779b9u^(uint32_t)y*0x85ebca6bu^(seed*0xc2b2ae35u);
 v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return v;
}
inline float random01(int x,int y,uint32_t seed) noexcept {
 return (hash(x,y,seed)>>8)*(1.f/16777216.f);
}
inline float burn(float b,float s)noexcept{
 return s<=0.f?0.f:1.f-(1.f-b)/s;
}
inline float dodge(float b,float s)noexcept{
 return s>=1.f?1.f:b/(1.f-s);
}
inline float softLight(float b,float s)noexcept{
 if(s<=.5f)return b-(1.f-2.f*s)*b*(1.f-b);
 const float d=b<=.25f?((16.f*b-12.f)*b+4.f)*b:std::sqrt(std::max(0.f,b));
 return b+(2.f*s-1.f)*(d-b);
}
inline float channel(float b,float s,Mode mode) noexcept {
 // b: background / source image, s: gradient / foreground
 switch(mode){
 case Mode::Replace:case Mode::Normal:case Mode::Dissolve:case Mode::DancingDissolve:return s;
 case Mode::Darken:case Mode::DarkerColor:return std::min(b,s);
 case Mode::Multiply:return b*s;
 case Mode::ColorBurn:return burn(b,s);
 case Mode::ClassicColorBurn:return 1.f-std::min(1.f,(1.f-b)/std::max(s,1.e-8f));
 case Mode::LinearBurn:return b+s-1.f;
 case Mode::Add:case Mode::LinearDodge:return b+s;
 case Mode::Lighten:case Mode::LighterColor:return std::max(b,s);
 case Mode::Screen:return b+s-b*s;
 case Mode::ColorDodge:return dodge(b,s);
 case Mode::ClassicColorDodge:return std::min(1.f,b/std::max(1.f-s,1.e-8f));
 case Mode::Overlay:return b<=.5f?2.f*b*s:1.f-2.f*(1.f-b)*(1.f-s);
 case Mode::SoftLight:return softLight(b,s);
 case Mode::HardLight:return s<=.5f?2.f*b*s:1.f-2.f*(1.f-b)*(1.f-s);
 case Mode::LinearLight:return b+2.f*s-1.f;
 case Mode::VividLight:return s<=.5f?burn(b,2.f*s):dodge(b,2.f*(s-.5f));
 case Mode::PinLight:return s<=.5f?std::min(b,2.f*s):std::max(b,2.f*s-1.f);
 case Mode::HardMix:return (channel(b,s,Mode::VividLight)<.5f)?0.f:1.f;
 case Mode::Difference:case Mode::ClassicDifference:return std::abs(b-s);
 case Mode::Exclusion:return b+s-2.f*b*s;
 case Mode::Subtract:return b-s;
 case Mode::Divide:return std::abs(s)<1.e-8f?0.f:b/s;
 default:return s;
 }
}
struct RGB {float r,g,b;};
inline float lum(RGB v)noexcept{return .3f*v.r+.59f*v.g+.11f*v.b;}
inline float sat(RGB v)noexcept{
 return std::max({v.r,v.g,v.b})-std::min({v.r,v.g,v.b});
}
inline RGB clipColor(RGB c)noexcept {
 const float l=lum(c),n=std::min({c.r,c.g,c.b}),x=std::max({c.r,c.g,c.b});
 if(n<0.f){
  const float t=(l-n)!=0.f?l/(l-n):0.f;
  c={l+(c.r-l)*t,l+(c.g-l)*t,l+(c.b-l)*t};
 }
 if(x>1.f){
  const float t=(x-l)!=0.f?(1.f-l)/(x-l):0.f;
  c={l+(c.r-l)*t,l+(c.g-l)*t,l+(c.b-l)*t};
 }
 return c;
}
inline RGB setLum(RGB c,float l)noexcept{
 const float d=l-lum(c);return clipColor({c.r+d,c.g+d,c.b+d});
}
inline RGB setSat(RGB c,float desired)noexcept {
 float v[3]={c.r,c.g,c.b};
 int i0=0,i1=1,i2=2;
 if(v[i0]>v[i1])std::swap(i0,i1);
 if(v[i1]>v[i2])std::swap(i1,i2);
 if(v[i0]>v[i1])std::swap(i0,i1);
 if(v[i2]>v[i0]){
  const float mid=(v[i1]-v[i0])*desired/(v[i2]-v[i0]);
  v[i2]=desired;v[i1]=mid;
 }else{v[i2]=0;v[i1]=0;}
 v[i0]=0;
 return {v[0],v[1],v[2]};
}
inline RGB colors(RGB background,RGB foreground,Mode mode)noexcept {
 if(mode==Mode::DarkerColor)
  return lum(background)<=lum(foreground)?background:foreground;
 if(mode==Mode::LighterColor)
  return lum(background)>=lum(foreground)?background:foreground;
 if(mode==Mode::Hue)return setLum(setSat(foreground,sat(background)),lum(background));
 if(mode==Mode::Saturation)return setLum(setSat(background,sat(foreground)),lum(background));
 if(mode==Mode::Color)return setLum(foreground,lum(background));
 if(mode==Mode::Luminosity)return setLum(background,lum(foreground));
 return {channel(background.r,foreground.r,mode),
         channel(background.g,foreground.g,mode),
         channel(background.b,foreground.b,mode)};
}
inline float alpha(float aSrc,float aGradient,Mode mode,int x,int y,uint32_t frame)noexcept{
 const float a=unit(aSrc),g=unit(aGradient);
 switch(mode){
 case Mode::Replace:return g;
 case Mode::Normal:return g+a*(1.f-g);
 case Mode::Dissolve:return random01(x,y,0)<g?1.f:a;
 case Mode::DancingDissolve:return random01(x,y,frame+1u)<g?1.f:a;
 default:return unit(channel(a,g,mode));
 }
}
inline Color compose(Color srcPremult,Color grad,Config cfg,int x=0,int y=0)noexcept{
 const float as=unit(srcPremult.a),ag=unit(grad.a);
 const float ao=alpha(as,ag,cfg.alpha,x,y,cfg.frameSeed);
 // Common modes need no source unpremultiplication or HSL math at all.
 // This is algebraically identical to generic premultiplied source-over.
 if(cfg.color==Mode::Replace)
  return {sane(grad.r)*ao,sane(grad.g)*ao,sane(grad.b)*ao,ao};
 if(cfg.color==Mode::Normal){
  const float normalA=ag+as*(1.f-ag);
  if(normalA<=1.e-10f)return {0,0,0,ao};
  const float factor=ao/normalA;
  const float srcWeight=1.f-ag;
  const float sr=as>1.e-10f?sane(srcPremult.r):0.f;
  const float sg=as>1.e-10f?sane(srcPremult.g):0.f;
  const float sb=as>1.e-10f?sane(srcPremult.b):0.f;
  return {(sr*srcWeight+sane(grad.r)*ag)*factor,
          (sg*srcWeight+sane(grad.g)*ag)*factor,
          (sb*srcWeight+sane(grad.b)*ag)*factor,ao};
 }
 const float invAs=as>1.e-10f?1.f/as:0.f;
 const RGB src{as>0.f?sane(srcPremult.r)*invAs:0.f,
               as>0.f?sane(srcPremult.g)*invAs:0.f,
               as>0.f?sane(srcPremult.b)*invAs:0.f};
 const RGB fg{sane(grad.r),sane(grad.g),sane(grad.b)};
 RGB straight=fg;
 if(cfg.color!=Mode::Replace){
  float coverage=ag;
  if(cfg.color==Mode::Dissolve)
   coverage=random01(x,y,0)<ag?1.f:0.f;
  else if(cfg.color==Mode::DancingDissolve)
   coverage=random01(x,y,cfg.frameSeed+1u)<ag?1.f:0.f;
  const RGB blended=colors(src,fg,cfg.color);
  const float normalA=coverage+as*(1.f-coverage);
  if(normalA>1.e-10f){
   const float sourceW=as*(1.f-coverage);
   const float foregroundW=coverage*(1.f-as);
   const float overlap=coverage*as;
   straight={
    (src.r*sourceW+fg.r*foregroundW+blended.r*overlap)/normalA,
    (src.g*sourceW+fg.g*foregroundW+blended.g*overlap)/normalA,
    (src.b*sourceW+fg.b*foregroundW+blended.b*overlap)/normalA
   };
  }else straight={0,0,0};
 }
 return {sane(straight.r)*ao,sane(straight.g)*ao,sane(straight.b)*ao,ao};
}
} // namespace gradation::blend
