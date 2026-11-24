#pragma once
// ColorPicker.slugui interaction model. Pure, side-effect-free math:
// compatible with Gradation's existing stopui::RGBA / HSV and 32bpc storage.
// Mode semantics reproduce AE's Hue/Saturation/Brightness/RGB radio selection.
// Axes are left-to-right x=[0,1] and top-to-bottom y=[0,1].
#include "stop_color_picker_model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gradation::picker {
using stopui::RGBA;
using stopui::HSV;
enum class AxisMode:uint8_t {Hue, Saturation, Brightness, Red, Green, Blue, Alpha};
enum class WidgetState:uint8_t {Normal,Hover,Pressed,Focus,Disabled};
constexpr std::array<const char*,7> modeNames{"H","S","B","R","G","B","A"};
constexpr int axisIndex(AxisMode mode) noexcept{return static_cast<int>(mode);}
inline float unit(float x)noexcept{return stopui::uiClamp(x);}
inline float hue01(HSV v)noexcept{
 return std::clamp(v.h/360.f,0.f,1.f);
}
inline float barY(AxisMode m,RGBA c,HSV h)noexcept {
 switch(m){
 case AxisMode::Hue:return 1.f-hue01(h);
 case AxisMode::Saturation:return 1.f-unit(h.s);
 case AxisMode::Brightness:return 1.f-unit(h.v);
 case AxisMode::Red:return 1.f-unit(c.r);
 case AxisMode::Green:return 1.f-unit(c.g);
 case AxisMode::Blue:return 1.f-unit(c.b);
 case AxisMode::Alpha:return 1.f-unit(c.a);
 }
 return 0.f;
}
struct Coord{float x=0.f,y=0.f;};
inline Coord planePosition(AxisMode m,RGBA c,HSV h)noexcept{
 switch(m){
 case AxisMode::Hue:return {unit(h.s),1.f-unit(h.v)};
 case AxisMode::Saturation:return {hue01(h),1.f-unit(h.v)};
 case AxisMode::Brightness:return {hue01(h),1.f-unit(h.s)};
 case AxisMode::Red:return {unit(c.g),1.f-unit(c.b)};
 case AxisMode::Green:return {unit(c.r),1.f-unit(c.b)};
 case AxisMode::Blue:return {unit(c.r),1.f-unit(c.g)};
 case AxisMode::Alpha:return {hue01(h),1.f-unit(h.s)};
 }
 return {};
}
inline RGBA samplePlane(AxisMode m,RGBA c,HSV h,float x,float y)noexcept{
 x=unit(x);y=unit(y);
 switch(m){
 case AxisMode::Hue:
   h.s=x;h.v=1.f-y;return stopui::fromHSV(h,c.a);
 case AxisMode::Saturation:
   h.h=360.f*x;h.v=1.f-y;return stopui::fromHSV(h,c.a);
 case AxisMode::Brightness:
   h.h=360.f*x;h.s=1.f-y;return stopui::fromHSV(h,c.a);
 case AxisMode::Red:c.g=x;c.b=1.f-y;return c;
 case AxisMode::Green:c.r=x;c.b=1.f-y;return c;
 case AxisMode::Blue:c.r=x;c.g=1.f-y;return c;
 case AxisMode::Alpha:
   h.h=360.f*x;h.s=1.f-y;return stopui::fromHSV(h,c.a);
 }
 return c;
}
inline RGBA sampleBar(AxisMode m,RGBA c,HSV h,float y)noexcept{
 const float value=1.f-unit(y);
 switch(m){
 case AxisMode::Hue:h.h=360.f*value;return stopui::fromHSV(h,c.a);
 case AxisMode::Saturation:h.s=value;return stopui::fromHSV(h,c.a);
 case AxisMode::Brightness:h.v=value;return stopui::fromHSV(h,c.a);
 case AxisMode::Red:c.r=value;return c;
 case AxisMode::Green:c.g=value;return c;
 case AxisMode::Blue:c.b=value;return c;
 case AxisMode::Alpha:c.a=value;return c;
 }
 return c;
}
inline HSV deriveHSV(RGBA c,HSV last)noexcept{
 HSV next=stopui::toHSV(c);
 if(next.s<1.e-6f)next.h=last.h; // preserve hue on greys
 return next;
}
inline void applyPlane(AxisMode m,RGBA& c,HSV& h,float x,float y)noexcept{
 x=unit(x);y=unit(y);
 // HSV is the authoritative state for H/S/B and A views. On greys
 // and pure black, reverse RGB->HSV loses the other latent channels;
 // keep their actual selected values even while the RGB preview cannot
 // distinguish them. This matches the native Adobe color picker.
 switch(m){
 case AxisMode::Hue:h.s=x;h.v=1.f-y;break;
 case AxisMode::Saturation:h.h=360.f*x;h.v=1.f-y;break;
 case AxisMode::Brightness:h.h=360.f*x;h.s=1.f-y;break;
 case AxisMode::Alpha:h.h=360.f*x;h.s=1.f-y;break;
 default:
  c=samplePlane(m,c,h,x,y);
  h=deriveHSV(c,h);
  return;
 }
 c=stopui::fromHSV(h,c.a);
}
inline void applyBar(AxisMode m,RGBA& c,HSV& h,float y)noexcept{
 const float value=1.f-unit(y);
 switch(m){
 case AxisMode::Hue:h.h=360.f*value;break;
 case AxisMode::Saturation:h.s=value;break;
 case AxisMode::Brightness:h.v=value;break;
 case AxisMode::Alpha:c.a=value;return;
 default:
  c=sampleBar(m,c,h,y);
  h=deriveHSV(c,h);
  return;
 }
 c=stopui::fromHSV(h,c.a);
}
struct SpectrumColors {
 uint32_t fill=0;
 uint32_t text=0;
 uint32_t outline=0;
 uint32_t focus=0;
 bool focusRing=false;
};
inline SpectrumColors buttonColors(WidgetState s,bool primary)noexcept{
 if(s==WidgetState::Disabled)return {0x303030,0x707070,0x303030,0,false};
 if(primary){
  if(s==WidgetState::Pressed)return {0x0056a5,0xffffff,0x0056a5,0,false};
  if(s==WidgetState::Hover)return {0x0060c8,0xffffff,0x0060c8,0,false};
  if(s==WidgetState::Focus)return {0x066ce7,0xffffff,0x066ce7,0x4095f2,true};
  return {0x066ce7,0xffffff,0x066ce7,0,false};
 }
 if(s==WidgetState::Pressed)return {0x454545,0xf5f5f5,0x8d8d8d,0,false};
 if(s==WidgetState::Hover)return {0x2d2d2d,0xdddddd,0x6f6f6f,0,false};
 if(s==WidgetState::Focus)return {0x1d1d1d,0xe5e5e5,0x545454,0x4095f2,true};
 return {0x1d1d1d,0xb0b0b0,0x454545,0,false};
}
inline SpectrumColors radioColors(WidgetState s,bool selected)noexcept{
 if(s==WidgetState::Disabled)
  return {selected?0x606060u:0x505050u,0x777777,0x606060,0,false};
 if(s==WidgetState::Pressed)return {selected?0xddddddU:0x252525U,
         0xf3f3f3,0xf0f0f0,0,false};
 if(s==WidgetState::Hover)return {selected?0xf3f3f3U:0x292929U,
         0xf5f5f5,0xd0d0d0,0,false};
 if(s==WidgetState::Focus)return {selected?0xeaeaeaU:0x1d1d1dU,
         0xffffff,0xf4f4f4,0x4095f2,true};
 return {selected?0xd5d5d5U:0x1d1d1dU,0xf2f2f2,0xb0b0b0,0,false};
}
} // namespace gradation::picker
