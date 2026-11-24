#include "raster_core.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::blend;
using namespace gradation::raster;
static int failures=0;
static void check(bool v,const char* msg){
 if(!v){std::cerr<<msg<<"\n";failures++;}
}
static bool near(float a,float b,float eps=1.e-5f){return std::abs(a-b)<=eps;}
static void close(Color a,Color b,float tol,const char* context){
 check(near(a.r,b.r,tol)&&near(a.g,b.g,tol)&&near(a.b,b.b,tol)&&near(a.a,b.a,tol),context);
}
int main(){
 {
  Color source{.24f,.12f,.04f,.4f}; // premult source: straight {.6,.3,.1}
  Color gradient{.9f,.1f,.4f,.5f};
  Config cfg;
  cfg.alpha=Mode::Multiply;cfg.color=Mode::Replace;
  close(compose(source,gradient,cfg),{.18f,.02f,.08f,.2f},1e-6f,"replace+multiply");
  cfg.alpha=Mode::Replace;
  close(compose(source,gradient,cfg),{.45f,.05f,.2f,.5f},1e-6f,"replace+replace");
  cfg.alpha=Mode::Normal;
  check(near(compose(source,gradient,cfg).a,.7f),"over alpha 0.7");
  cfg.alpha=Mode::Add;
  check(near(compose(source,gradient,cfg).a,.9f),"add alpha 0.9");
  cfg.alpha=Mode::Screen;
  check(near(compose(source,gradient,cfg).a,.7f),"screen alpha 0.7");
  cfg.alpha=Mode::Multiply;cfg.color=Mode::Normal;
  Color result=compose(source,gradient,cfg);
  check(near(result.a,.2f),"normal color with independent multiply alpha");
  const float weightedR=(.24f*(1.f-.5f)+.9f*.5f)/(.5f+.4f*(1.f-.5f));
  check(near(result.r,.2f*weightedR),"normal foreground-over color normalized then premultiplied");
  cfg.alpha=Mode::Normal;cfg.color=Mode::Normal;
  close(compose({0,0,0,0},gradient,cfg),{.45f,.05f,.2f,.5f},1e-6f,
        "transparent input still yields RGBA with normal alpha");
  close(compose(source,{0,0,0,0},cfg),source,1e-6f,
        "zero-alpha gradient leaves source untouched with Normal");
 }
 for(int mode=0;mode<ColorChoices;mode++){
  Config cfg;cfg.color=(Mode)mode;cfg.alpha=Mode::Multiply;
  for(int alphaMode=0;alphaMode<AlphaChoices;alphaMode++){
   cfg.alpha=(Mode)alphaMode;cfg.frameSeed=128;
   for(float sa: {0.f,.02f,.3f,.7f,1.f})
    for(float ga: {0.f,.1f,.7f,1.f}){
     const Color in{sa*.4f,sa*.7f,sa*.9f,sa};
     const Color grad{.6f,.2f,.8f,ga};
     const Color result=compose(in,grad,cfg,171,21);
     check(std::isfinite(result.a)&&result.a>=0.f&&result.a<=1.f,
           "alpha nonfinite/out of range");
     check(std::isfinite(result.r)&&std::isfinite(result.g)&&std::isfinite(result.b),
           "RGB nonfinite");
    }
  }
 }
 {
  Config cfg;cfg.alpha=Mode::Dissolve;cfg.color=Mode::Dissolve;
  for(int x=0;x<1000;x++){
   auto a=compose({.15f,.12f,.10f,.5f},{.4f,.8f,.2f,.35f},cfg,x,10);
   auto b=compose({.15f,.12f,.10f,.5f},{.4f,.8f,.2f,.35f},cfg,x,10);
   close(a,b,0.f,"Dissolve deterministic per pixel");
  }
  cfg.color=Mode::DancingDissolve;cfg.alpha=Mode::DancingDissolve;cfg.frameSeed=3;
  int changes=0;
  for(int x=0;x<1000;x++){
   auto a=compose({.15f,.12f,.10f,.5f},{.4f,.8f,.2f,.35f},cfg,x,10);
   cfg.frameSeed=4;
   auto b=compose({.15f,.12f,.10f,.5f},{.4f,.8f,.2f,.35f},cfg,x,10);
   cfg.frameSeed=3;
   if(!near(a.a,b.a))changes++;
  }
  check(changes>100,"Dancing Dissolve responds to frame");
 }
 {
  // Float output retains HDR excursions for Add and Subtract.
  Config cfg;cfg.color=Mode::Add;cfg.alpha=Mode::Replace;
  const Color high=compose({1.2f,0,0,1.f},{.9f,0,0,1.f},cfg);
  check(high.r>1.9f,"32bpc additive HDR RGB retained");
  cfg.color=Mode::Subtract;
  const Color neg=compose({.3f,0,0,1.f},{.9f,0,0,1.f},cfg);
  check(neg.r<-.59f,"32bpc subtract negative RGB retained");
 }
 {
  // All modes must render at 8/16/32bpc and produce requested alpha.
  constexpr int W=67,H=24;
  Settings s;s.shape=Shape::Linear;s.startX=0;s.startY=0;s.endX=67;s.endY=0;
  Gradient g({},{{0,{.1f,.2f,1.f,.1f}},{1,{1.f,.4f,.1f,.9f}}});
  Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
  PackedRamp8 p8=makePacked8(ramp);
  PackedRamp16 p16=makePacked16(ramp);
  for(int depth=0;depth<3;depth++){
   const int bpp=depth==0?4:depth==1?8:16;
   std::vector<uint8_t> input((size_t)W*H*bpp,0),output(input.size(),0xcd);
   for(int i=0;i<W*H;i++){
    const float a=(i%7)/6.f;
    if(depth==0){
     Pixel8 p{u8(a),u8(a*.2f),u8(a*.7f),u8(a*.4f)};
     std::memcpy(input.data()+(size_t)i*4,&p,4);
    }else if(depth==1){
     Pixel16 p{(uint16_t)std::lround(a*32768),
               (uint16_t)std::lround(a*.2f*32768),
               (uint16_t)std::lround(a*.7f*32768),
               (uint16_t)std::lround(a*.4f*32768)};
     std::memcpy(input.data()+(size_t)i*8,&p,8);
    }else{
     Pixel32 p{a,a*.2f,a*.7f,a*.4f};
     std::memcpy(input.data()+(size_t)i*16,&p,16);
    }
   }
   Buffer in{input.data(),W,H,W*bpp,0,0};
   WritableBuffer out{output.data(),W,H,W*bpp,0,0};
   for(int alphaMode=0;alphaMode<AlphaChoices;alphaMode++)
    for(int colorMode=0;colorMode<ColorChoices;colorMode++){
     Config cfg{(Mode)colorMode,(Mode)alphaMode,25};
     check(render(in,out,(Depth)depth,s,ramp,&p8,&p16,cfg),"render all blend combinations");
     for(int i:{0,1,3,33,66,67,219,300}){
      float a=0;
      if(depth==0)a=((Pixel8*)output.data())[i].a/255.f;
      if(depth==1)a=((Pixel16*)output.data())[i].a/32768.f;
      if(depth==2)a=((Pixel32*)output.data())[i].a;
      check(std::isfinite(a)&&a>=0.f&&a<=1.f,"rendered alpha finite");
     }
    }
  }
 }
 std::cout<<"Blend modes: "<<ColorChoices<<" color x "<<AlphaChoices<<" alpha, 3 depths: failures="<<failures<<"\n";
 return failures?1:0;
}
