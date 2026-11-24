#pragma once
// Host-independent RGB / HSV math for Gradation's 2D color picker.
#include <algorithm>
#include <cmath>
namespace gradation::stopui {
struct RGB {float r=0,g=0,b=0;};
struct HSV {float h=0,s=0,v=0;};
inline float clip(float x) noexcept {
 return std::isfinite(x)?std::clamp(x,0.f,1.f):0.f;
}
inline HSV toHSV(RGB c) noexcept {
 const float hi=std::max({c.r,c.g,c.b}),lo=std::min({c.r,c.g,c.b});
 const float delta=hi-lo;
 HSV out{};
 out.v=hi;
 out.s=hi>0.f?delta/hi:0.f;
 if(delta<1e-12f)return out;
 if(hi==c.r)out.h=(c.g-c.b)/delta;
 else if(hi==c.g)out.h=2.f+(c.b-c.r)/delta;
 else out.h=4.f+(c.r-c.g)/delta;
 out.h*=60.f;
 if(out.h<0)out.h+=360.f;
 return out;
}
inline RGB fromHSV(HSV c) noexcept {
 float h=std::fmod(std::isfinite(c.h)?c.h:0.f,360.f);
 if(h<0)h+=360.f;
 const float s=clip(c.s),v=clip(c.v);
 const float chroma=v*s,sector=h/60.f;
 const float x=chroma*(1.f-std::abs(std::fmod(sector,2.f)-1.f));
 const float m=v-chroma;
 RGB out{};
 if(sector<1)out={chroma,x,0};
 else if(sector<2)out={x,chroma,0};
 else if(sector<3)out={0,chroma,x};
 else if(sector<4)out={0,x,chroma};
 else if(sector<5)out={x,0,chroma};
 else out={chroma,0,x};
 return {out.r+m,out.g+m,out.b+m};
}
}
