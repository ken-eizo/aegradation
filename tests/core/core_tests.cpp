#include "../../src/core/gradient_core.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace gradation;
int main(){
 Gradient g;
 auto a=g.sample(0),b=g.sample(1),m=g.sample(.5f);
 assert(a.r==1&&b.r==0&&std::abs(m.r-.5f)<1e-6f);
 assert(std::abs(g.at(.5f,0).r-.5f)<1e-6f);
 for(auto s:{Space::SRGB,Space::LinearSRGB,Space::HSV,Space::HSL,Space::Oklab,Space::Oklch,Space::YCbCr,Space::YCgCo}){
  Gradient q({Shape::Linear,s},{{0,{.1f,.2f,.3f,1}},{1,{.8f,.7f,.6f,1}}});
  for(float t:{0.f,.25f,.5f,.75f,1.f}){
   auto c=q.sample(t);assert(std::isfinite(c.r)&&std::isfinite(c.g)&&std::isfinite(c.b));
  }
 }
 Settings s;s.shape=Shape::Mapping;s.loop=true;
 Gradient mapped(s);assert(std::abs(mapped.at(0,0,.25f).r-.75f)<1e-6f);
 std::cout<<"core tests passed\n";
}
