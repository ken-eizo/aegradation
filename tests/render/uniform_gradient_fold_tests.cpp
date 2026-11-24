#include "../../src/render/cpu/raster_core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 Settings original{};
 Gradient gradient(original,{{0,{.21f,.72f,.91f,.65f}},
                              {.37f,{.21f,.72f,.91f,.65f}},
                              {1,{.21f,.72f,.91f,.65f}}});
 Ramp ramp{};
 for(int i=0;i<=4096;i++)ramp[(size_t)i]=gradient.sample(i/4096.f);
 const auto p8=makePacked8(ramp);
 const auto p16=makePacked16(ramp);
 const blend::Config modes[]={
  {blend::Mode::Replace,blend::Mode::Replace,0},
  {blend::Mode::Normal,blend::Mode::Multiply,0},
  {blend::Mode::Screen,blend::Mode::Normal,0},
  {blend::Mode::Add,blend::Mode::Multiply,0}
 };
 constexpr int W=147,H=89;
 int checked=0;
 for(int depth=0;depth<3;depth++){
  const int bpp=depth==0?4:depth==1?8:16;
  const int stride=(W+11)*bpp;
  std::vector<uint8_t> source((size_t)stride*H);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const float alpha=(x%11==0)?0.2f:1.f;
   uint8_t* cell=source.data()+(size_t)y*stride+x*bpp;
   if(depth==0){
    const Pixel8 p{u8(alpha),u8(alpha*.4f),u8(alpha*.72f),u8(alpha*.19f)};
    std::memcpy(cell,&p,4);
   }else if(depth==1){
    const Pixel16 p{(uint16_t)std::lround(alpha*32768),
                    (uint16_t)std::lround(alpha*.4f*32768),
                    (uint16_t)std::lround(alpha*.72f*32768),
                    (uint16_t)std::lround(alpha*.19f*32768)};
    std::memcpy(cell,&p,8);
   }else{
    const Pixel32 p{alpha,alpha*.4f,alpha*.72f,alpha*.19f};
    std::memcpy(cell,&p,16);
   }
  }
  for(int shape=0;shape<4;shape++)for(const auto mode:modes){
   Settings s=original;s.shape=(Shape)shape;
   s.startX=40.5f;s.startY=65.5f;s.endX=121.2f;s.endY=8.3f;
   s.offsetPx=-27.f;s.scalePercent=151.3f;s.reflect=true;
   Settings folded=s;
   folded.shape=Shape::Linear;
   folded.startX=0;folded.startY=0;folded.endX=1;folded.endY=0;
   folded.offsetPx=0;folded.scalePercent=100;
   folded.loop=false;folded.reflect=false;folded.invert=false;
   std::vector<uint8_t> ref(source.size(),0xaa),opt(source.size(),0xbb);
   Buffer in{source.data(),W-9,H-7,stride,4,6};
   WritableBuffer a{ref.data(),W,H,stride,0,0};
   WritableBuffer b{opt.data(),W,H,stride,0,0};
   if(!render(in,a,(Depth)depth,s,ramp,&p8,&p16,mode)||
      !render(in,b,(Depth)depth,folded,ramp,&p8,&p16,mode))return 2;
   for(int y=0;y<H;y++)for(int x=0;x<W;x++)for(int c=0;c<4;c++){
    const size_t at=(size_t)y*stride+x*bpp+c*(bpp/4);
    double delta=0;
    if(depth==0)delta=std::abs((int)ref[at]-(int)opt[at]);
    else if(depth==1){
     uint16_t v,w;std::memcpy(&v,ref.data()+at,2);
     std::memcpy(&w,opt.data()+at,2);
     delta=std::abs((int)v-(int)w);
    }else{
     float v,w;std::memcpy(&v,ref.data()+at,4);
     std::memcpy(&w,opt.data()+at,4);
     delta=std::abs((double)v-(double)w);
    }
    const double tol=depth==0?2:depth==1?6:0.00015;
    if(!(delta<=tol)){
     std::cerr<<"FAIL depth "<<depth<<" shape "<<shape
       <<" blend "<<(int)mode.color<<" err "<<delta
       <<" pixel "<<x<<","<<y<<" channel "<<c<<"\n";return 3;
    }
   }
   ++checked;
  }
 }
 std::cout<<"PASS "<<checked<<" uniform-sRGB constant-ramp mode/shape/depth "
          <<"comparisons with independent unfused raster; cropped source/hdr\n";
}
