#include "gradient_custom_ui_layout.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gradation::customui;
static void test(bool ok,const char* msg){
 if(!ok)throw std::runtime_error(msg);
}
static bool near(float a,float b,float epsilon=.001f){
 return std::fabs(a-b)<=epsilon;
}
int main(){
 try{
  // Screenshot: 4K/150%, 440x119 physical. FHD at 100% must be
  // 220x59.5 physical, preserving the proportion of display coverage.
  const Size u4k=requestedLogicalSize(3840,2160,144);
  const Size u1080=requestedLogicalSize(1920,1080,96);
  const Size u1080_150=requestedLogicalSize(1920,1080,144);
  test(u4k.width==293&&u4k.height==79,"4K 150% logical size");
  test(u1080.width==220&&u1080.height==60,"FHD 100% logical size");
  test(u1080_150.width==147&&u1080_150.height==40,
       "FHD 150% must not be incorrectly enlarged");
  const float physical4k=float(u4k.width)*1.5f;
  const float physical1080=float(u1080.width);
  const float physical1080_150=float(u1080_150.width)*1.5f;
  test(std::fabs(physical4k/3840.f-physical1080/1920.f)<.0003f,
       "4K/FHD width fraction mismatch");
  test(std::fabs(physical1080_150/1920.f-physical1080/1920.f)<.0003f,
       "FHD monitor 150% fraction mismatch");
  // Layout follows the EXACT 440x119 exchange coordinates.
  const View v=fit(0,0,440,119);
  test(near(v.x,0)&&near(v.y,0)&&near(v.scale,1),"reference viewport");
  const Rect r=bar(v);
  test(near(r.x,26)&&near(r.y,33)&&near(r.w,389)&&near(r.h,47),
       "Figma bar physical geometry");
  test(near(pinLeft(v,0),13)&&near(pinLeft(v,.5f),207.5f)&&
       near(pinLeft(v,1),402),"Figma pin x coordinates");
  const View half=fit(0,0,220,59.5f);
  test(near(half.scale,.5f)&&near(bar(half).w,194.5f)&&
       near(pinLeft(half,1),201),"FHD scaled authored position");
  // Three themes reference the actual screenshots: #1d, #32, #f8.
  test(themeFromGray(29)==Theme::Dark,"dark theme");
  test(themeFromGray(50)==Theme::Mid,"mid theme");
  test(themeFromGray(248)==Theme::Light,"light theme");
  test(near(colors(Theme::Dark).bg,29.f/255.f)&&
       near(colors(Theme::Mid).bg,50.f/255.f)&&
       near(colors(Theme::Light).bg,248.f/255.f),
       "Screenshot theme background RGB");
  std::cout<<"PASS: native Custom UI 440x119 geometry, 4K/FHD+DPI layout, 3 AE themes\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL: "<<e.what()<<'\n';
  return 1;
 }
}
