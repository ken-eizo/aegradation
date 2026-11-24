#pragma once
#include <algorithm>
#include <cstddef>
#include <string_view>
#include <slugia/vector_atlas.hpp>
namespace gradation::picker {
// Figma source: Source Sans Pro Regular 19px, 178 occupies 29px and
// 90 occupies 19px. This shared hit/paint model keeps suffixes, caret
// and selection geometry in one consistent Figma coordinate system.
inline float numericGlyphWidth(char c) noexcept {
 if(c=='.'||c==',')return 4.8f;
 if(c=='-')return 6.5f;
 if(c=='+')return 7.f;
 return 9.55f;
}
inline float numericTextWidth(std::string_view text,std::size_t count=std::string_view::npos) noexcept {
 float result=0.f;
 const auto end=std::min(text.size(),count);
 for(std::size_t i=0;i<end;++i)result+=numericGlyphWidth(text[i]);
 return result;
}
inline float numericEditableWidth(std::string_view text) noexcept {
 return std::clamp(numericTextWidth(text)+1.f,10.f,79.f);
}
inline bool numericValueContains(std::string_view text,float x,float y,float rowY) noexcept {
 return x>=600.f && x<600.f+numericEditableWidth(text) &&
        y>=rowY && y<rowY+24.f;
}
inline std::size_t numericTextHit(std::string_view text,float x) noexcept {
 float cursor=0.f;
 for(std::size_t i=0;i<text.size();++i){
  const float advance=numericGlyphWidth(text[i]);
  if(x<cursor+advance*.5f)return i;
  cursor+=advance;
 }
 return text.size();
}
// Actual atlas advances, including shaped glyph clusters/kerning.
inline float pickerTextAdvance(const slugia::VectorAtlas* atlas,
 std::string_view text,std::size_t count,bool hex=false) {
 count=std::min(count,text.size());
 const float size=hex?17.f:19.f;
 const std::uint16_t weight=hex?400:600;
 const char* style=hex?"Regular":"SemiBold";
 if(atlas&&atlas->built()){
  if(auto run=atlas->shapeText(text,"Source Sans Pro",weight,false,style)){
   float x=0.f;
   for(const auto& glyph:run->glyphs)
    if(glyph.cluster<count)x+=glyph.xAdvance*size;
   return x;
  }
  float x=0.f;unsigned prev=0;
  for(std::size_t i=0;i<count;++i){
   unsigned cp=static_cast<unsigned char>(text[i]);
   if(prev)x+=atlas->kerning(prev,cp,"Source Sans Pro",weight,false,style)*size;
   auto m=atlas->metrics(atlas->glyph(cp,"Source Sans Pro",weight,false,style));
   x+=(m?m->advance:.6f)*size;prev=cp;
  }
  return x;
 }
 return hex?float(count)*(49.f/6.f):numericTextWidth(text,count);
}
inline std::size_t pickerTextHit(const slugia::VectorAtlas* atlas,
 std::string_view text,float x,bool hex=false) {
 if(x<=0)return 0;
 float prev=0;
 for(std::size_t i=1;i<=text.size();++i){
  float next=pickerTextAdvance(atlas,text,i,hex);
  if(x<(prev+next)*.5f)return i-1;
  prev=next;
 }
 return text.size();
}
inline float pickerNumericWidth(const slugia::VectorAtlas* atlas,std::string_view text){
 return std::clamp(pickerTextAdvance(atlas,text,text.size())+1.f,10.f,79.f);
}
} // namespace gradation::picker
