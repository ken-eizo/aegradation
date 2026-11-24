#include "picker_axes.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace gradation::picker;
static void require(bool value,const char* msg){
 if(!value){std::cerr<<"FAIL "<<msg<<"\n";std::exit(1);}
}
static bool close(float a,float b,float e=.002f){return std::abs(a-b)<e;}
int main(){
 const RGBA cyan{.091f,.894f,.875f,.7f};
 HSV hsv=gradation::stopui::toHSV(cyan);
 for(int axis=0;axis<7;axis++){
  const auto mode=(AxisMode)axis;
  const auto p=planePosition(mode,cyan,hsv);
  const auto fromPlane=samplePlane(mode,cyan,hsv,p.x,p.y);
  const auto fromBar=sampleBar(mode,cyan,hsv,barY(mode,cyan,hsv));
  require(close(fromPlane.r,cyan.r)&&close(fromPlane.g,cyan.g)&&
          close(fromPlane.b,cyan.b)&&close(fromPlane.a,cyan.a),
          "all seven axes plane marker exactly reproduces selected color");
  require(close(fromBar.r,cyan.r)&&close(fromBar.g,cyan.g)&&
          close(fromBar.b,cyan.b)&&close(fromBar.a,cyan.a),
          "all seven axes bar marker exactly reproduces selected color");
 }
 {RGBA c=cyan;HSV h=hsv;
  applyBar(AxisMode::Saturation,c,h,1.f);
  require(close(h.s,0.f)&&close(c.r,c.g)&&close(c.g,c.b),
          "S bar bottom is greyscale and hue is preserved");
  applyPlane(AxisMode::Saturation,c,h,.3333333f,0.f);
  require(close(h.h,120.f,.01f)&&close(h.v,1.f),
          "S square x selects Hue and y selects Brightness");
 }
 {RGBA c=cyan;HSV h=hsv;
  applyBar(AxisMode::Brightness,c,h,1.f);
  require(close(c.r,0.f)&&close(c.g,0.f)&&close(c.b,0.f),
          "B bar bottom is pure black");
  applyPlane(AxisMode::Brightness,c,h,.66666667f,0.f);
  require(close(h.h,240.f,.01f)&&close(h.s,1.f,.001f),
          "B square x selects Hue and y selects Saturation");
 }
 {RGBA c=cyan;HSV h=hsv;
  applyBar(AxisMode::Red,c,h,0.f);
  require(close(c.r,1.f)&&close(c.g,cyan.g)&&close(c.b,cyan.b),
          "R bar only changes red");
  applyPlane(AxisMode::Red,c,h,0.f,1.f);
  require(close(c.r,1.f)&&close(c.g,0.f)&&close(c.b,0.f),
          "R square is green/blue");
 }
 {RGBA c=cyan;HSV h=hsv;
  applyBar(AxisMode::Alpha,c,h,1.f);
  require(close(c.a,0.f)&&close(c.r,cyan.r)&&close(c.g,cyan.g)&&
          close(c.b,cyan.b),
          "Alpha bar only changes alpha");
 }
 for(bool primary:{false,true}){
  const auto normal=buttonColors(WidgetState::Normal,primary);
  const auto hovered=buttonColors(WidgetState::Hover,primary);
  const auto touch=buttonColors(WidgetState::Pressed,primary);
  const auto focused=buttonColors(WidgetState::Focus,primary);
  const auto disabled=buttonColors(WidgetState::Disabled,primary);
  require(normal.fill!=hovered.fill&&hovered.fill!=touch.fill,
          "Normal Hover Touch are distinct");
  require(focused.focusRing&&focused.focus==0x4095f2,
          "focus ring is Spectrum blue");
  require(disabled.text!=normal.text&&!disabled.focusRing,
          "disabled control dims text");
 }
 for(bool selected:{false,true}){
  const auto normal=radioColors(WidgetState::Normal,selected);
  const auto hover=radioColors(WidgetState::Hover,selected);
  const auto press=radioColors(WidgetState::Pressed,selected);
  const auto focus=radioColors(WidgetState::Focus,selected);
  const auto disabled=radioColors(WidgetState::Disabled,selected);
  require(normal.fill!=hover.fill&&press.fill!=hover.fill,
          "radio states are distinct");
  require(focus.focusRing&&disabled.fill!=normal.fill,
          "radio focused and disabled distinct");
 }
 const auto active=radioColors(WidgetState::Normal,true);
 const auto inactive=radioColors(WidgetState::Normal,false);
 require(active.fill!=inactive.fill,"selected differs from unselected");
 std::cout<<"PASS: H/S/B RGB/A axes, 5-spectrum-states, 2 selection states\n";
 return 0;
}
