#pragma once
// Lightweight color model for the Gradation native Win32 SV/Hue/Alpha picker.
// UI only: never instantiated on an After Effects render/MFR thread.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <string>

namespace gradation::stopui {
struct RGBA {float r=0.f,g=0.f,b=0.f,a=1.f;};
struct HSV {float h=0.f,s=0.f,v=0.f;};
inline float uiClamp(float f) noexcept {
 return std::isfinite(f)?std::clamp(f,0.f,1.f):0.f;
}
inline HSV toHSV(RGBA c) noexcept {
 const float r=uiClamp(c.r),g=uiClamp(c.g),b=uiClamp(c.b);
 const float maximum=std::max({r,g,b});
 const float minimum=std::min({r,g,b});
 const float delta=maximum-minimum;
 HSV hsv{};
 hsv.v=maximum;
 hsv.s=maximum>0.f?delta/maximum:0.f;
 if(delta<=1e-8f)return hsv;
 if(maximum==r)hsv.h=60.f*std::fmod((g-b)/delta+6.f,6.f);
 else if(maximum==g)hsv.h=60.f*((b-r)/delta+2.f);
 else hsv.h=60.f*((r-g)/delta+4.f);
 if(hsv.h>=360.f)hsv.h-=360.f;
 if(hsv.h<0.f)hsv.h+=360.f;
 return hsv;
}
inline RGBA fromHSV(HSV hsv,float alpha=1.f) noexcept {
 float h=std::isfinite(hsv.h)?std::fmod(hsv.h,360.f):0.f;
 if(h<0.f)h+=360.f;
 const float s=uiClamp(hsv.s),v=uiClamp(hsv.v);
 const float c=v*s;
 const float h6=h/60.f;
 const float x=c*(1.f-std::abs(std::fmod(h6,2.f)-1.f));
 const float m=v-c;
 RGBA out{};
 if(h6<1.f)out={c,x,0.f,alpha};
 else if(h6<2.f)out={x,c,0.f,alpha};
 else if(h6<3.f)out={0.f,c,x,alpha};
 else if(h6<4.f)out={0.f,x,c,alpha};
 else if(h6<5.f)out={x,0.f,c,alpha};
 else out={c,0.f,x,alpha};
 out.r+=m;out.g+=m;out.b+=m;
 return out;
}
inline int rgb8(float f) noexcept {
 return (int)std::lround(uiClamp(f)*255.f);
}
inline std::wstring hexRGB(RGBA color) {
 wchar_t buffer[16]{};
 std::swprintf(buffer,16,L"%02X%02X%02X",
              rgb8(color.r),rgb8(color.g),rgb8(color.b));
 return buffer;
}
inline bool parseHexRGB(const wchar_t* text,RGBA& color) noexcept {
 if(!text)return false;
 while(std::iswspace(*text))++text;
 if(*text==L'#')++text;
 int values[6]{};
 for(int i=0;i<6;i++){
  const wchar_t c=*text++;
  if(c>=L'0'&&c<=L'9')values[i]=(int)(c-L'0');
  else if(c>=L'a'&&c<=L'f')values[i]=(int)(c-L'a'+10);
  else if(c>=L'A'&&c<=L'F')values[i]=(int)(c-L'A'+10);
  else return false;
 }
 while(std::iswspace(*text))++text;
 if(*text)return false;
 color.r=(values[0]*16+values[1])/255.f;
 color.g=(values[2]*16+values[3])/255.f;
 color.b=(values[4]*16+values[5])/255.f;
 return true;
}
} // namespace gradation::stopui
