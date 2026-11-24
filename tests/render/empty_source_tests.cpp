#include "../../src/render/cpu/raster_core.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 Settings s;s.startX=0;s.startY=0;s.endX=47;s.endY=0;
 Gradient g(s,{{0,{.1f,.8f,.3f,.4f}},{1,{1.f,.2f,.6f,.8f}}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 auto r8=makePacked8(ramp);
 auto r16=makePacked16(ramp);
 Buffer empty{nullptr,0,0,0,0,0};
 for(int depth=0;depth<3;depth++){
  const int bpp=depth==0?4:depth==1?8:16;
  std::vector<uint8_t> out((size_t)48*24*bpp,0xcd);
  WritableBuffer dst{out.data(),48,24,48*bpp,0,0};
  for(Shape shape:{Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping}){
   s.shape=shape;
   for(auto mode:{blend::Mode::Replace,blend::Mode::Normal,blend::Mode::Add,blend::Mode::Multiply}){
    std::fill(out.begin(),out.end(),0xcd);
    const blend::Config cfg{blend::Mode::Replace,mode,0};
    if(!render(empty,dst,(Depth)depth,s,ramp,&r8,&r16,cfg)){
     std::cerr<<"failed null source render\n";return 1;
    }
    double sumAlpha=0;
    for(int i=0;i<48*24;i++){
     float alpha=0;
     if(depth==0)alpha=((const Pixel8*)out.data())[i].a/255.f;
     else if(depth==1)alpha=((const Pixel16*)out.data())[i].a/32768.f;
     else alpha=((const Pixel32*)out.data())[i].a;
     if(!std::isfinite(alpha)||alpha<0||alpha>1){std::cerr<<"invalid alpha\n";return 2;}
     sumAlpha+=alpha;
    }
    if(mode==blend::Mode::Multiply&&sumAlpha!=0){
     std::cerr<<"multiply with empty source generated alpha\n";return 3;
    }
    if(mode!=blend::Mode::Multiply&&sumAlpha<=0){
     std::cerr<<"generated alpha was clipped away\n";return 4;
    }
   }
  }
 }
 std::cout<<"Transparent/null input: all 3 depths x 4 shapes x 4 alpha blends PASS\n";
}
