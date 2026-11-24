#include "../../src/core/gradient_core.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <array>
#include <vector>
using namespace gradation;
int main(){
 std::array<Space,14> spaces={
  Space::SRGB,Space::LinearSRGB,Space::HSV,Space::HSL,Space::HSI,
  Space::Oklab,Space::Oklch,Space::CMYK,Space::YCbCr,Space::YCgCo,
  Space::CIECAM02,Space::HCT,Space::LMS,Space::Spectral};
 std::array<Color,4> examples={Color{1,0,0,1},Color{0,1,0,1},
 Color{.25f,.65f,.9f,.75f},Color{.73f,.32f,.26f,.42f}};
 for(Space s:spaces){
  for(Color a:examples){
   Gradient q({Shape::Linear,s},{{0,a},{1,{.1f,.2f,.35f,1}}});
   for(int i=0;i<=100;i++){
    auto c=q.sample(i*.01f);
    if(!(std::isfinite(c.r)&&std::isfinite(c.g)&&std::isfinite(c.b)&&std::isfinite(c.a))){
     std::cerr<<"nonfinite mode "<<int(s)<<" t "<<i<<"\n";return 2;
    }
   }
   auto at0=q.sample(0),at1=q.sample(1);
   assert(std::abs(at0.r-a.r)<1e-7f && std::abs(at1.g-.2f)<1e-7f);
  }
 }
 for(Space s:{Space::HSV,Space::HSL,Space::HSI,Space::Oklab,Space::Oklch,
  Space::YCbCr,Space::YCgCo,Space::CIECAM02,Space::HCT,Space::LMS}){
  auto c=examples[2];auto y=unconvert(convert(c,s),s);
  std::cout<<"roundtrip mode="<<int(s)<<" RGB err="
           <<std::abs(y.r-c.r)+std::abs(y.g-c.g)+std::abs(y.b-c.b)<<"\n";
 }
 Settings st;st.shape=Shape::Linear;st.startX=0;st.endX=100;
 for(int k=0;k<4;k++){
  st.shape=(Shape)k;Gradient q(st);
  auto a=q.at(25,0,.25f);assert(std::isfinite(a.r));
 }
 std::cout<<"all 14 spaces finite and endpoints verified\n";
}
