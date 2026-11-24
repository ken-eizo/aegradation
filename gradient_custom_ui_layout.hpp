#pragma once
// Native Adobe Drawbot geometry from Gradient_CustomUIGenerated.slugui.
// Reference is 440x119 physical pixels on a 3840x2160 display at 150%.
// No Slugia dependency; the AE host retains control of event delivery/painting.
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace gradation::customui {
inline constexpr float kWidth=440.f,kHeight=119.f;
inline constexpr float kBarX=26.f,kBarY=33.f,kBarW=389.f,kBarH=47.f;
inline constexpr float kPinX=13.f,kPinY=43.f,kPinW=25.f,kPinH=50.f;
struct Size {int width,height;};
struct View {float x,y,scale;};
struct Rect {float x,y,w,h;};
inline float screenFraction(int monitorW,int monitorH) noexcept {
 if(monitorW<=0||monitorH<=0)return 1.f;
 return std::clamp(std::min(monitorW/3840.f,monitorH/2160.f),.25f,3.f);
}
// AE requests logical control dimensions; Windows/AE maps them to physical
// dimensions at the selected window DPI. Do not multiply by DPI twice.
inline Size requestedLogicalSize(int monitorW,int monitorH,int dpi) noexcept {
 const float f=screenFraction(monitorW,monitorH);
 const float pixelToLogical=96.f/float(dpi>0?dpi:96);
 return {std::max(1,int(std::lround(kWidth*f*pixelToLogical))),
         std::max(1,int(std::lround(kHeight*f*pixelToLogical)))};
}
inline View fit(float x,float y,float width,float height) noexcept {
 const float factor=std::max(0.f,std::min(width/kWidth,height/kHeight));
 return {x+(width-kWidth*factor)*.5f,
         y+(height-kHeight*factor)*.5f,
         factor};
}
inline Rect bar(View v) noexcept {
 return {v.x+kBarX*v.scale,v.y+kBarY*v.scale,
         kBarW*v.scale,kBarH*v.scale};
}
inline float pinLeft(View v,float position) noexcept {
 return v.x+(kPinX+kBarW*std::clamp(position,0.f,1.f))*v.scale;
}
inline float pinCenter(View v,float position) noexcept {
 return pinLeft(v,position)+kPinW*v.scale*.5f;
}
enum class Theme : std::uint8_t { Dark=0, Mid=1, Light=2 };
inline Theme themeFromGray(float gray8) noexcept {
 if(gray8>150.f)return Theme::Light;
 return gray8>=40.f?Theme::Mid:Theme::Dark;
}
struct Colors {float bg,stroke,fillEdge,markerEdge,checker0,checker1;};
inline Colors colors(Theme t) noexcept {
 switch(t){
 case Theme::Light:return {248.f/255.f,213.f/255.f,230.f/255.f,
                           109.f/255.f,1.f,213.f/255.f};
 case Theme::Mid:return {50.f/255.f,84.f/255.f,67.f/255.f,
                         178.f/255.f,1.f,84.f/255.f};
 default:return {29.f/255.f,75.f/255.f,52.f/255.f,
                 176.f/255.f,1.f,52.f/255.f};
 }
}
} // namespace gradation::customui
