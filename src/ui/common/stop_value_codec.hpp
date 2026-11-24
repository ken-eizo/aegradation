#pragma once
// UI conversion independent of Adobe and Win32, so the numeric behavior can be tested.
// AE 16bpc has 32768 channel levels (not 65535); 32bpc is normalized float.
#include <array>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <string>
#include <limits>
namespace gradation::stopui {
enum class Depth : int { Bpc8=8, Bpc16=16, Bpc32=32 };
inline int channelMax(Depth depth) noexcept {
 return depth==Depth::Bpc8?255:depth==Depth::Bpc16?32768:0;
}
inline float limit(float value)noexcept {
 return std::isfinite(value)?std::clamp(value,0.f,1.f):0.f;
}
inline std::wstring formatChannel(float value,Depth depth) {
 const float v=limit(value);
 wchar_t text[64]{};
 if(depth==Depth::Bpc32){
  // Display at most four digits AFTER the decimal point, like AE's
  // compact 32bpc channel UI. Only the displayed text is rounded: the
  // underlying float32 RGBA remains unchanged until the user edits it.
  std::swprintf(text,64,L"%.4f",(double)v);
  std::size_t length=std::wcslen(text);
  while(length>0&&text[length-1]==L'0')text[--length]=0;
  if(length>0&&text[length-1]==L'.')text[--length]=0;
  if(length==0)text[0]=L'0',text[1]=0;
 }else{
  std::swprintf(text,64,L"%d",(int)std::lround(v*channelMax(depth)));
 }
 return text;
}
inline bool parseChannel(const wchar_t* source,Depth depth,float& result) noexcept {
 if(!source)return false;
 while(std::iswspace(*source))++source;
 if(!*source)return false;
 wchar_t* end=nullptr;
 if(depth==Depth::Bpc32){
  const float f=std::wcstof(source,&end);
  if(end==source||!std::isfinite(f)||f<0.f||f>1.f)return false;
  while(std::iswspace(*end))++end;
  if(*end)return false;
  result=f;
 }else{
  if(*source==L'-'||*source==L'+')return false;
  unsigned long n=std::wcstoul(source,&end,10);
  if(end==source||n>(unsigned long)channelMax(depth))return false;
  while(std::iswspace(*end))++end;
  if(*end)return false;
  // Use the actual After Effects integer normalization for both depths.
  result=(float)n/(float)channelMax(depth);
 }
 return true;
}
inline bool parseRGBA(const std::array<std::wstring,4>& display,Depth depth,
                      const std::array<bool,4>& changed,
                      const std::array<float,4>& original,
                      std::array<float,4>& result) noexcept {
 result=original;
 for(int i=0;i<4;i++){
  if(changed[i]&&!parseChannel(display[i].c_str(),depth,result[i]))return false;
 }
 return true;
}
} // namespace gradation::stopui
