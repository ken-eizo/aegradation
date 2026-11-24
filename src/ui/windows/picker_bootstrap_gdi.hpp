#pragma once
// Instant CPU first paint while Vulkan initializes. Never changes the
// authored SlugUI tree or the final GPU rendering. Avoids showing a
// seconds-long empty black modal between WM_INITDIALOG and first GPU frame.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "picker_raster.hpp"
#include "../common/picker_screen_scale.hpp"
#include "picker_theme.hpp"
#include "picker_text_metrics.hpp"

namespace gradation::picker {
inline void drawPickerBootstrap(HDC target,const PickerState& state,
                                int targetWidth,int targetHeight) {
 if(!target||targetWidth<=0||targetHeight<=0)return;
 BITMAPINFO bmi{};
 bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
 bmi.bmiHeader.biWidth=kDesignedWidth;
 bmi.bmiHeader.biHeight=-kDesignedHeight;
 bmi.bmiHeader.biPlanes=1;
 bmi.bmiHeader.biBitCount=32;
 bmi.bmiHeader.biCompression=BI_RGB;
 HDC memory=CreateCompatibleDC(target);
 if(!memory)return;
 void* bits=nullptr;
 HBITMAP surface=CreateDIBSection(target,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
 if(!surface||!bits){if(surface)DeleteObject(surface);DeleteDC(memory);return;}
 const HGDIOBJ previous=SelectObject(memory,surface);
 auto* rgba=static_cast<std::uint32_t*>(bits);
 const auto palette=pickerPalette(state.theme);
 const std::uint32_t background=palette.background;
 const std::uint32_t border=palette.border;
 const auto rgb=[](std::uint32_t c){return RGB((c>>16)&255,(c>>8)&255,c&255);};
 std::fill_n(rgba,std::size_t(kDesignedWidth)*kDesignedHeight,background);
 auto rectangle=[&](int x,int y,int w,int h,std::uint32_t color){
  const int l=std::clamp(x,0,kDesignedWidth),r=std::clamp(x+w,0,kDesignedWidth);
  const int t=std::clamp(y,0,kDesignedHeight),b=std::clamp(y+h,0,kDesignedHeight);
  for(int row=t;row<b;++row)
   std::fill(rgba+std::size_t(row)*kDesignedWidth+l,
             rgba+std::size_t(row)*kDesignedWidth+r,color);
 };
 rectangle(23,28,388,388,border);
 rectangle(436,28,29,388,border);
 rectangle(490,28,29,388,border);
 rectangle(544,28,86,97,border);
 std::vector<float> samples{};
 auto image=[&](int x,int y,int w,int h,int kind) {
  samples.resize(std::size_t(w)*h*4u);
  rasterizePickerImage(state,w,h,kind,std::span<float>(samples));
  for(int row=0;row<h;++row)for(int col=0;col<w;++col){
   const std::size_t off=(std::size_t(row)*w+col)*4u;
   const auto channel=[&](float v)->std::uint32_t{
    return std::uint32_t(std::lround(std::clamp(v,0.f,1.f)*255.f));
   };
   rgba[std::size_t(y+row)*kDesignedWidth+x+col]
    =(channel(samples[off+1])<<16)|
     (channel(samples[off+2])<<8)|channel(samples[off+3]);
  }
 };
 // Identical numeric RGB buffers to the final Slugia external images.
 image(24,29,386,386,0);
 image(437,29,27,386,1);
 image(491,29,27,386,2);
 rectangle(545,29,84,47,0x00000000u);
 const auto initial=state.initial;
 const auto initialColor=[](float r,float g,float b)->std::uint32_t{
  const auto ch=[](float f)->unsigned{return unsigned(std::lround(std::clamp(f,0.f,1.f)*255.f));};
  return (ch(r)<<16)|(ch(g)<<8)|ch(b);
 };
 rectangle(545,29,84,47,initialColor(initial.r,initial.g,initial.b));
 // Split lower preview follows authored Figma's 48.5px midpoint.
 image(545,77,84,47,3);
 // Interactive elements: lightweight native first-paint approximations
 // are replaced by the exact SlugUI glyphs/shapes after Vulkan is ready.
 HFONT font=CreateFontW(-19,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
                       DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Source Sans Pro");
 const HGDIOBJ oldFont=font?SelectObject(memory,font):nullptr;
 SetBkMode(memory,TRANSPARENT);
 auto text=[&](int x,int y,const std::wstring& value,COLORREF color){
  SetTextColor(memory,color);
  TextOutW(memory,x,y,value.data(),int(value.size()));
 };
 const std::array<wchar_t,7> channelNames{L'H',L'S',L'B',L'R',L'G',L'B',L'A'};
 const std::array<int,7> ys{145,177,209,249,281,313,345};
 for(std::size_t k=0;k<ys.size();++k) {
  // Native GDI circles are only used before the actual SlugUI frame.
  HBRUSH bg=CreateSolidBrush(rgb(palette.background));
  HPEN edge=CreatePen(PS_SOLID,2,rgb(palette.normal));
  HGDIOBJ ob=SelectObject(memory,bg),op=SelectObject(memory,edge);
  Ellipse(memory,545,ys[k]+3,562,ys[k]+20);
  if(int(k)==axisIndex(state.axis)){
   auto mark=CreateSolidBrush(rgb(palette.normal));
   const auto oldBrush=SelectObject(memory,mark);
   Ellipse(memory,550,ys[k]+8,557,ys[k]+15);
   SelectObject(memory,oldBrush);DeleteObject(mark);
  }
  SelectObject(memory,ob);SelectObject(memory,op);
  DeleteObject(bg);DeleteObject(edge);
  text(576,ys[k]-1,std::wstring(1,channelNames[k]),rgb(palette.text));
  text(601,ys[k]-1,std::wstring(state.fields[k].begin(),state.fields[k].end()),rgb(palette.number));
  const std::wstring digits(state.fields[k].begin(),state.fields[k].end());
  SIZE measure{};GetTextExtentPoint32W(memory,digits.data(),int(digits.size()),&measure);
  const int suffixX=601+measure.cx+3;
  if(k==0)text(suffixX,ys[k]-1,L"°",rgb(palette.text));
  else if(k<3)text(suffixX,ys[k]-1,L"%",rgb(palette.text));
 }
 text(556,385,L"#",rgb(palette.text));
 rectangle(571,384,82,32,palette.focus);
 rectangle(574,387,76,26,palette.field);
 const std::wstring hex(state.fields[7].begin(),state.fields[7].end());
 SIZE hexWidth{};GetTextExtentPoint32W(memory,hex.data(),int(hex.size()),&hexWidth);
 rectangle(584,391,std::max(1,int(hexWidth.cx)),18,palette.selection);
 text(584,389,hex,rgb(palette.selectedText));
 rectangle(674,29,100,35,0x00066ce7u);
 text(710,35,L"OK",RGB(255,255,255));
 text(690,88,L"Cancel",rgb(palette.text));
 text(711,225,L"Preview",rgb(palette.text));
 if(oldFont)SelectObject(memory,oldFont);
 if(font)DeleteObject(font);
 // Fill letterboxed space with the active palette; never anisotropically
 // stretch the authored scene while the Vulkan renderer starts.
 RECT client{0,0,targetWidth,targetHeight};
 HBRUSH bg=CreateSolidBrush(rgb(background));
 if(bg){FillRect(target,&client,bg);DeleteObject(bg);}
 const float scale=pickerClientScale(targetWidth,targetHeight);
 SetStretchBltMode(target,COLORONCOLOR);
 StretchBlt(target,0,0,int(std::lround(kDesignedWidth*scale)),
            int(std::lround(kDesignedHeight*scale)),
            memory,0,0,kDesignedWidth,kDesignedHeight,SRCCOPY);
 SelectObject(memory,previous);
 DeleteObject(surface);
 DeleteDC(memory);
}
}
