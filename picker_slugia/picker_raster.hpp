#pragma once
// Exact CPU RGBA32f raster generation for the Gradation color picker.
// No heap allocation, Vulkan, global mutable state, or per-pixel HSV bar conversions.
#include "picker_ui.hpp"
#include <span>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace gradation::picker {

// Exact dependency keys for the four raster surfaces. Do not regenerate
// a 386x386 plane merely because the marker moved on that plane.
// Bitwise float keys preserve signed zero and avoid approximate invalidation.
using PickerRasterKey=std::array<std::uint32_t,5>;
inline PickerRasterKey pickerRasterKey(const PickerState& s,int kind) noexcept {
 const auto bits=[](float x){return std::bit_cast<std::uint32_t>(x);};
 const auto c=s.current;
 const auto h=s.hsv;
 const auto key=[&](float x=0.f,float y=0.f,float z=0.f,float a=0.f){
  return PickerRasterKey{(std::uint32_t)s.axis,bits(x),bits(y),bits(z),bits(a)};
 };
 if(kind==0){
  switch(s.axis){
   case AxisMode::Hue:return key(h.h);
   case AxisMode::Saturation:return key(h.s,c.a);
   case AxisMode::Brightness:case AxisMode::Alpha:return key(h.v,c.a);
   case AxisMode::Red:return key(c.r,c.a);
   case AxisMode::Green:return key(c.g,c.a);
   case AxisMode::Blue:return key(c.b,c.a);
  }
 }
 if(kind==1){
  switch(s.axis){
   case AxisMode::Hue:return key();
   case AxisMode::Saturation:return key(h.h,h.v,c.a);
   case AxisMode::Brightness:return key(h.h,h.s,c.a);
   case AxisMode::Red:return key(c.g,c.b,c.a);
   case AxisMode::Green:return key(c.r,c.b,c.a);
   case AxisMode::Blue:return key(c.r,c.g,c.a);
   case AxisMode::Alpha:return key(c.r,c.g,c.b);
  }
 }
 if(kind==2)return {0,bits(c.r),bits(c.g),bits(c.b),0};
 const auto checked=s.preview?c:s.initial;
 return {0,bits(checked.r),bits(checked.g),bits(checked.b),bits(checked.a)};
}
// out has 4 floats/pixel in the existing Vulkan-owned image order: A,R,G,B.
// kind: 0=plane, 1=color channel bar, 2=alpha/checker, 3=preview/checker.
inline void rasterizePickerImage(const PickerState& state, int width, int height,
                                 int kind, std::span<float> out) {
 if(width<=0||height<=0||kind<0||kind>3||
    out.size()<size_t(width)*size_t(height)*4U)
  throw std::invalid_argument("Gradation raster: invalid dimensions or buffer");
 const auto opaque=stopui::fromHSV({state.hsv.h,1.f,1.f},1.f);
 const auto checked=state.preview?state.current:state.initial;
 const float previewA=stopui::uiClamp(checked.a);
 const float previewInv=1.f-previewA;
 const float previewR=stopui::uiClamp(checked.r)*previewA;
 const float previewG=stopui::uiClamp(checked.g)*previewA;
 const float previewB=stopui::uiClamp(checked.b)*previewA;
 const float cr=stopui::uiClamp(state.current.r);
 const float cg=stopui::uiClamp(state.current.g);
 const float cb=stopui::uiClamp(state.current.b);
 auto* dst=out.data();
 // Fixed Figma 386px color plane; only the x-dependent hue sector and
 // triangle fraction can be cached, never the final 32f colors.
 if(kind==0&&width<=386&&
    (state.axis==AxisMode::Saturation||
     state.axis==AxisMode::Brightness||
     state.axis==AxisMode::Alpha)){
  std::array<float,386> hueTriangle{};
  std::array<std::uint8_t,386> hueSector{};
  for(int x=0;x<width;++x){
   const float u=(float(x)+.5f)/float(width);
   float h=std::fmod(360.f*u,360.f);
   if(h<0.f)h+=360.f;
   const float h6=h/60.f;
   hueTriangle[(size_t)x]=1.f-std::abs(std::fmod(h6,2.f)-1.f);
   hueSector[(size_t)x]=static_cast<std::uint8_t>(
    h6<1.f?0:h6<2.f?1:h6<3.f?2:h6<4.f?3:h6<5.f?4:5);
  }
  for(int y=0;y<height;++y){
   const float yv=(float(y)+.5f)/float(height);
   const float sat=stopui::uiClamp(state.axis==AxisMode::Saturation?
                                    state.hsv.s:1.f-yv);
   const float val=stopui::uiClamp(state.axis==AxisMode::Saturation?
                                    1.f-yv:state.hsv.v);
   const float chroma=val*sat;
   const float remainder=val-chroma;
   for(int x=0;x<width;++x){
    const float intermediate=chroma*hueTriangle[(size_t)x];
    RGBA c{};
    switch(hueSector[(size_t)x]){
     case 0:c={chroma,intermediate,0.f,state.current.a};break;
     case 1:c={intermediate,chroma,0.f,state.current.a};break;
     case 2:c={0.f,chroma,intermediate,state.current.a};break;
     case 3:c={0.f,intermediate,chroma,state.current.a};break;
     case 4:c={intermediate,0.f,chroma,state.current.a};break;
     default:c={chroma,0.f,intermediate,state.current.a};break;
    }
    c.r+=remainder;c.g+=remainder;c.b+=remainder;
    *dst++=stopui::uiClamp(c.a);
    *dst++=stopui::uiClamp(c.r);
    *dst++=stopui::uiClamp(c.g);
    *dst++=stopui::uiClamp(c.b);
   }
  }
  return;
 }
 for(int y=0;y<height;++y) {
  const float v=(float(y)+.5f)/float(height);
  const float oneMinusV=1.f-v;
  if(kind==1) {
   // The horizontal channel bar is constant for every x at a fixed y.
   // Previous loop repeated a full HSV->RGB conversion 27 times/row.
   const RGBA c=state.axis==AxisMode::Hue
       ?stopui::fromHSV({360.f*oneMinusV,1.f,1.f},1.f)
       :sampleBar(state.axis,state.current,state.hsv,v);
   const float a=stopui::uiClamp(c.a),r=stopui::uiClamp(c.r);
   const float g=stopui::uiClamp(c.g),b=stopui::uiClamp(c.b);
   for(int x=0;x<width;++x) {
    *dst++=a;*dst++=r;*dst++=g;*dst++=b;
   }
   continue;
  }
  if(kind==2) {
   const float a=oneMinusV;
   const float inv=1.f-a;
   const float r=cr*a,g=cg*a,b=cb*a;
   for(int x=0;x<width;++x) {
    constexpr float tile=31.f/3.f; // Figma authored 10.333333px checker
    const int xi=int(std::floor((float(x)+.5f)/tile));
    const int yi=int(std::floor((float(y)+.5f)/tile));
    const float checker=((xi+yi)&1)==0?1.f:213.f/255.f;
    *dst++=1.f;
    *dst++=stopui::uiClamp(checker*inv+r);
    *dst++=stopui::uiClamp(checker*inv+g);
    *dst++=stopui::uiClamp(checker*inv+b);
   }
   continue;
  }
  if(kind==3) {
   for(int x=0;x<width;++x) {
    constexpr float tile=31.f/3.f; // Figma authored 10.333333px checker
    // 172x97 is exactly 2x the 86x48.5 half-height source geometry.
    const float sampleScale=(width==172&&height==97)?2.f:1.f;
    const int xi=int(std::floor((float(x)+.5f)/(tile*sampleScale)));
    const int yi=int(std::floor((float(y)+.5f)/(tile*sampleScale)));
    const float checker=((xi+yi)&1)==0?1.f:213.f/255.f;
    *dst++=1.f;
    *dst++=stopui::uiClamp(checker*previewInv+previewR);
    *dst++=stopui::uiClamp(checker*previewInv+previewG);
    *dst++=stopui::uiClamp(checker*previewInv+previewB);
   }
   continue;
  }
  if(state.axis==AxisMode::Hue) {
   const float value=oneMinusV;
   for(int x=0;x<width;++x) {
    const float u=(float(x)+.5f)/float(width);
    const RGBA c={value*((1.f-u)+u*opaque.r),
                  value*((1.f-u)+u*opaque.g),
                  value*((1.f-u)+u*opaque.b),1.f};
    *dst++=stopui::uiClamp(c.a);
    *dst++=stopui::uiClamp(c.r);
    *dst++=stopui::uiClamp(c.g);
    *dst++=stopui::uiClamp(c.b);
   }
  }else{
   for(int x=0;x<width;++x){
    const float u=(float(x)+.5f)/float(width);
    const RGBA c=samplePlane(state.axis,state.current,state.hsv,u,v);
    *dst++=stopui::uiClamp(c.a);
    *dst++=stopui::uiClamp(c.r);
    *dst++=stopui::uiClamp(c.g);
    *dst++=stopui::uiClamp(c.b);
   }
  }
 }
}
} // namespace gradation::picker
