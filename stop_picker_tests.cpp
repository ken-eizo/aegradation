#include "stop_color_picker_model.hpp"
#include "stop_value_codec.hpp"
#include "picker_slugia/picker_axes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace gradation::stopui;
static bool approximatelyEqual(float a,float b,float tolerance=0.000003f){
 return std::abs(a-b)<=tolerance;
}
int main(){
 // High-precision RGB must survive opening/canceling and editing only
 // hue/alpha controls. 8bpc HEX conversion is explicit, not implicit.
 // 32bpc input stays full precision, presentation is max 4 decimals.
 if(formatChannel(.806543f,Depth::Bpc32)!=L"0.8065"||
    formatChannel(.5f,Depth::Bpc32)!=L"0.5"||
    formatChannel(1.f,Depth::Bpc32)!=L"1"||
    formatChannel(0.f,Depth::Bpc32)!=L"0"||
    formatChannel(.00001f,Depth::Bpc32)!=L"0")
  return 55;
 float precise=0.f;
 if(!parseChannel(L"0.80654321",Depth::Bpc32,precise)||
    !approximatelyEqual(precise,.80654321f,0.0000001f))return 56;
 const RGBA original{.123456789f,.234567891f,.345678912f,.456789123f};
 for(auto depth:{Depth::Bpc8,Depth::Bpc16,Depth::Bpc32}){
  const float value=.456789123f;
  const auto text=formatChannel(value,depth);
  float parsed=-1.f;
  if(!parseChannel(text.c_str(),depth,parsed))return 1;
  const float tol=depth==Depth::Bpc8?.002f:
                  depth==Depth::Bpc16?.00003f:.000051f;
  if(!approximatelyEqual(value,parsed,tol))return 2;
  std::array<float,4> input{original.r,original.g,original.b,original.a},output{};
  const std::array<std::wstring,4> values{
   formatChannel(original.r,depth),formatChannel(original.g,depth),
   formatChannel(original.b,depth),formatChannel(original.a,depth)};
  if(!parseRGBA(values,depth,{false,false,false,false},input,output)||
     std::memcmp(input.data(),output.data(),sizeof(input))!=0)return 3;
 }
 RGBA hex{0.f,0.f,0.f,.278f};
 if(!parseHexRGB(L"#123aBC",hex))return 4;
 if(hexRGB(hex)!=L"123ABC"||!approximatelyEqual(hex.a,.278f))return 5;
 for(const wchar_t* invalid:{L"",L"FFA",L"1122334",L"#GGAAFF",
                             L"12345Z",L"-012345",L"12 3456"}){
  RGBA unchanged=hex;
  if(parseHexRGB(invalid,unchanged)||
     std::memcmp(&unchanged,&hex,sizeof(RGBA))!=0)return 6;
 }
 for(const RGBA color:std::array<RGBA,9>{{
  {0,0,0,1},{1,1,1,.6f},{1,0,0,.4f},{0,1,0,1},
  {0,0,1,.9f},{1,.2f,.9f,.23f},{.1f,.8f,.26f,.7f},
  {.63f,.12f,.74f,.9f},{.1234567f,.9876543f,.555555f,.1234f}
 }}){
  const HSV h=toHSV(color);
  const RGBA again=fromHSV(h,color.a);
  if(!approximatelyEqual(color.r,again.r)||!approximatelyEqual(color.g,again.g)||
     !approximatelyEqual(color.b,again.b)||!approximatelyEqual(color.a,again.a))
     return 7;
 }
 for(int i=0;i<10000;i++){
  const RGBA color{(i%251)/250.f,((i*7)%239)/238.f,
                   ((i*43)%233)/232.f,.2678f};
  const HSV h=toHSV(color);
  const RGBA rt=fromHSV(h,color.a);
  if(!approximatelyEqual(color.r,rt.r)||!approximatelyEqual(color.g,rt.g)||
     !approximatelyEqual(color.b,rt.b)||!approximatelyEqual(color.a,rt.a))
     return 8;
 }
 using namespace gradation::picker;
 for(int axis=0;axis<7;++axis){
  auto color=original;
  auto hsv=toHSV(color);
  const auto mode=(AxisMode)axis;
  const float alpha=color.a;
  applyPlane(mode,color,hsv,.91f,.07f);
  if(!approximatelyEqual(alpha,color.a))return 19;
  applyBar(mode,color,hsv,0.f);
  if(mode!=AxisMode::Alpha&&!approximatelyEqual(alpha,color.a))return 20;
  if(mode==AxisMode::Alpha){
   if(!approximatelyEqual(color.a,1.f))return 21;
   applyBar(mode,color,hsv,1.f);
   if(!approximatelyEqual(color.a,0.f))return 22;
  }
 }
 std::cout<<"PASS: current picker color model, 10000 HSV round trips, HEX, numeric precision, all seven axes, no GUI launch\n";
 return 0;
}
