#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace gradation {
struct Color { float r=0,g=0,b=0,a=1; };
struct Stop { float position=0; Color color; };
enum class Shape { Linear, Radial, Angle, Mapping };
enum class Space { SRGB, LinearSRGB, HSV, HSL, HSI, Oklab, Oklch, CMYK, YCbCr, YCgCo, CIECAM02, HCT, LMS, Spectral };
struct Settings {
 Shape shape=Shape::Linear; Space space=Space::SRGB;
 float startX=0,startY=0,endX=1,endY=0,offsetPx=0,scalePercent=100;
 bool loop=false,reflect=false,invert=false;
};
inline float clamp(float x){return std::clamp(x,0.f,1.f);}
inline float srgbDecode(float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);}
inline float srgbEncode(float v){return v<=.0031308f?12.92f*v:1.055f*std::pow(std::max(v,0.f),1.f/2.4f)-.055f;}
inline Color toOklab(Color c){
 float r=srgbDecode(c.r),g=srgbDecode(c.g),b=srgbDecode(c.b);
 float l=std::cbrt(.4122214708f*r+.5363325363f*g+.0514459929f*b);
 float m=std::cbrt(.2119034982f*r+.6806995451f*g+.1073969566f*b);
 float s=std::cbrt(.0883024619f*r+.2817188376f*g+.6299787005f*b);
 return {.2104542553f*l+.793617785f*m-.0040720468f*s,1.9779984951f*l-2.428592205f*m+.4505937099f*s,.0259040371f*l+.7827717662f*m-.808675766f*s,c.a};
}
inline Color fromOklab(Color c){
 float l=c.r+.3963377774f*c.g+.2158037573f*c.b;
 float m=c.r-.1055613458f*c.g-.0638541728f*c.b;
 float s=c.r-.0894841775f*c.g-1.291485548f*c.b;
 l=l*l*l;m=m*m*m;s=s*s*s;
 return {srgbEncode(4.0767416621f*l-3.3077115913f*m+.2309699292f*s),srgbEncode(-1.2684380046f*l+2.6097574011f*m-.3413193965f*s),srgbEncode(-.0041960863f*l-.7034186147f*m+1.707614701f*s),c.a};
}
inline Color toHSV(Color c) {
 float hi=std::max({c.r,c.g,c.b}),lo=std::min({c.r,c.g,c.b}),delta=hi-lo;
 float h=0;
 if(delta>1e-10f){if(hi==c.r)h=(c.g-c.b)/delta;else if(hi==c.g)h=2+(c.b-c.r)/delta;else h=4+(c.r-c.g)/delta;h=std::fmod(h/6+1,1.f);}
 return {h,hi>0?delta/hi:0,hi,c.a};
}
inline Color fromHSV(Color c){
 float h=(c.r-std::floor(c.r))*6, chroma=c.b*c.g, x=chroma*(1-std::abs(std::fmod(h,2.f)-1)),m=c.b-chroma;
 float r=0,g=0,b=0;
 if(h<1){r=chroma;g=x;}else if(h<2){r=x;g=chroma;}else if(h<3){g=chroma;b=x;}else if(h<4){g=x;b=chroma;}else if(h<5){r=x;b=chroma;}else{r=chroma;b=x;}
 return {r+m,g+m,b+m,c.a};
}
inline Color toHSL(Color c){auto v=toHSV(c);float l=(std::max({c.r,c.g,c.b})+std::min({c.r,c.g,c.b}))*.5f;float delta=std::max({c.r,c.g,c.b})-std::min({c.r,c.g,c.b});return {v.r,delta/(1-std::abs(2*l-1)+1e-20f),l,c.a};}
inline Color fromHSL(Color c){float v=c.b+c.g*std::min(c.b,1-c.b);return fromHSV({c.r,v>0?2*(v-c.b)/v:0,v,c.a});}
#include "color_math.hpp"
inline Color convert(Color c,Space s){
 switch(s){
 case Space::LinearSRGB:return {srgbDecode(c.r),srgbDecode(c.g),srgbDecode(c.b),c.a};
 case Space::HSV:return toHSV(c);
 case Space::HSI:return detail::rgbToHSI(c);
 case Space::HSL:return toHSL(c);
 case Space::Oklab:return toOklab(c);
 case Space::Oklch:{auto o=toOklab(c);return {o.r,std::hypot(o.g,o.b),std::atan2(o.b,o.g),o.a};}
 case Space::YCbCr:return {.299f*c.r+.587f*c.g+.114f*c.b,-.168736f*c.r-.331264f*c.g+.5f*c.b,.5f*c.r-.418688f*c.g-.081312f*c.b,c.a};
 case Space::YCgCo:return {.25f*c.r+.5f*c.g+.25f*c.b,-.25f*c.r+.5f*c.g-.25f*c.b,.5f*c.r-.5f*c.b,c.a};
 case Space::LMS:return detail::rgbToLMS(c);
 case Space::CIECAM02:return detail::toAppearance(c,false);
 case Space::HCT:return detail::rgbToHCT(c);
 default:return c;
 }
}
inline Color unconvert(Color c,Space s){
 switch(s){
 case Space::LinearSRGB:return {srgbEncode(c.r),srgbEncode(c.g),srgbEncode(c.b),c.a};
 case Space::HSV:return fromHSV(c);
 case Space::HSI:return detail::hsiToRgb(c);
 case Space::HSL:return fromHSL(c);
 case Space::Oklab:return fromOklab(c);
 case Space::Oklch:return fromOklab({c.r,c.g*std::cos(c.b),c.g*std::sin(c.b),c.a});
 case Space::YCbCr:return {c.r+1.402f*c.b,c.r-.344136f*c.g-.714136f*c.b,c.r+1.772f*c.g,c.a};
 case Space::YCgCo:return {c.r-c.g+c.b,c.r+c.g,c.r-c.g-c.b,c.a};
 case Space::LMS:return detail::lmsToRgb(c);
 case Space::CIECAM02:return detail::fromAppearance(c,false);
 case Space::HCT:return detail::hctToRgb(c);
 default:return c;
 }
}
class Gradient {
 std::vector<Stop> stops_;
 Settings settings_;
 std::vector<detail::spectral38::Pigment> spectralStops_;
 // Cache the transformed colors once per gradient construction rather
 // than invoking RGB->Oklab/HSV/CIECAM02/HCT/etc. for all 4097 LUT samples.
 // Pure sample interpolation remains bit-equivalent; no global state.
 std::vector<Color> convertedStops_;
public:
 explicit Gradient(Settings s={},std::vector<Stop> stops={{0,{1,1,1,1}},{1,{0,0,0,1}}}):stops_(std::move(stops)),settings_(s){
 std::stable_sort(stops_.begin(),stops_.end(),[](const Stop&a,const Stop&b){return a.position<b.position;});
  if(settings_.space==Space::Spectral){
   spectralStops_.reserve(stops_.size());
   for(const auto& stop:stops_)spectralStops_.push_back(detail::spectral38::fromRgb(stop.color));
  }
#if !defined(GRADATION_DISABLE_COLOR_PRECONVERT)
  if(settings_.space!=Space::Spectral&&settings_.space!=Space::CMYK){
   convertedStops_.reserve(stops_.size());
   for(const Stop& stop:stops_)convertedStops_.push_back(convert(stop.color,settings_.space));
  }
#endif
 }
 float coordinate(float x,float y,float luminance=0)const{
 float dx=settings_.endX-settings_.startX,dy=settings_.endY-settings_.startY;
 float len=std::max(std::hypot(dx,dy),1e-8f),t=0;
 switch(settings_.shape){
 case Shape::Linear:t=((x-settings_.startX)*dx+(y-settings_.startY)*dy)/(len*len);break;
 case Shape::Radial:t=std::hypot(x-settings_.startX,y-settings_.startY)/len;break;
 case Shape::Angle:t=(std::atan2(y-settings_.startY,x-settings_.startX)-std::atan2(dy,dx))/(6.28318530718f);t-=std::floor(t);break;
 case Shape::Mapping:t=luminance;break;
 }
 t=(t*100.f/std::max(settings_.scalePercent,1e-6f))+settings_.offsetPx/len;
 if(settings_.loop||settings_.reflect){t=t-std::floor(t/(settings_.reflect?2.f:1.f))*(settings_.reflect?2.f:1.f);if(settings_.reflect)t=1-std::abs(1-t);}
 else t=clamp(t);
 return settings_.invert?1-t:t;
 }
 Color sample(float t)const{
 if(stops_.empty())return {};
 if(t<=stops_.front().position)return stops_.front().color;
 if(t>=stops_.back().position)return stops_.back().color;
 auto it=std::upper_bound(stops_.begin(),stops_.end(),t,[](float v,const Stop&s){return v<s.position;});
 const auto &b=*it,&a=*(it-1);
 float u=clamp((t-a.position)/std::max(b.position-a.position,1e-10f));
 if(settings_.space==Space::Spectral){
   const size_t idx=(size_t)(it-stops_.begin()-1);
   return detail::spectral38::mix(spectralStops_[idx],spectralStops_[idx+1],
                                  u,a.color.a+(b.color.a-a.color.a)*u);
  }
 if(settings_.space==Space::CMYK){
  auto encode=[](Color c) {
   float k=1-std::max({c.r,c.g,c.b});
   if(k>=.999999f)return std::array<float,4>{0,0,0,1};
   float div=1.f-k;
   return std::array<float,4>{(1-c.r-k)/div,(1-c.g-k)/div,(1-c.b-k)/div,k};
  };
  auto q=encode(a.color),r=encode(b.color);
  float c=q[0]+(r[0]-q[0])*u,m=q[1]+(r[1]-q[1])*u,y=q[2]+(r[2]-q[2])*u,k=q[3]+(r[3]-q[3])*u;
  return {(1-c)*(1-k),(1-m)*(1-k),(1-y)*(1-k),a.color.a+(b.color.a-a.color.a)*u};
 }
 Color x{},y{};
#if !defined(GRADATION_DISABLE_COLOR_PRECONVERT)
 if(!convertedStops_.empty()){
  const size_t index=(size_t)(it-stops_.begin()-1);
  x=convertedStops_[index];y=convertedStops_[index+1];
 }else
#endif
 {
  x=convert(a.color,settings_.space);
  y=convert(b.color,settings_.space);
 }
 if(settings_.space==Space::HSV||settings_.space==Space::HSL||settings_.space==Space::HSI||settings_.space==Space::HCT){float dh=y.r-x.r;if(dh>.5f)y.r-=1;else if(dh<-.5f)y.r+=1;}
 if(settings_.space==Space::CIECAM02){float dh=y.b-x.b;if(dh>.5f)y.b-=1;else if(dh<-.5f)y.b+=1;}
 if(settings_.space==Space::Oklch){float dh=y.b-x.b;if(dh>3.14159265f)y.b-=6.2831853f;else if(dh< -3.14159265f)y.b+=6.2831853f;}
 Color v{x.r+(y.r-x.r)*u,x.g+(y.g-x.g)*u,x.b+(y.b-x.b)*u,a.color.a+(b.color.a-a.color.a)*u};
 return unconvert(v,settings_.space);
 }
 Color at(float x,float y,float luminance=0)const{return sample(coordinate(x,y,luminance));}
};
}
