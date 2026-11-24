#include "raster_core.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int W=43,H=19,PAD=32;
 Ramp ramp{};
 Settings s;s.startX=0;s.startY=0;s.endX=W;s.endY=H;
 Gradient g;
 for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
 auto packed8=makePacked8(ramp);
 auto packed16=makePacked16(ramp);
 const float nan=std::numeric_limits<float>::quiet_NaN();
 const float inf=std::numeric_limits<float>::infinity();
 std::vector<uint8_t> buf((size_t)(W*16+PAD)*H),output(buf.size());
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  Pixel32 p{1.f,.2f,.6f,.9f};
  if(x%7==0)p.a=nan;
  if(x%11==0)p.r=inf;
  if(x%13==0)p.b=-inf;
  if(x%17==0)p.g=nan;
  std::memcpy(buf.data()+(size_t)y*(W*16+PAD)+(size_t)x*16,&p,16);
 }
 Buffer in{buf.data(),W,H,W*16+PAD,0,0};
 WritableBuffer out{output.data(),W,H,W*16+PAD,0,0};
 for(Shape shape:{Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping}){
  s.shape=shape;
  if(!render(in,out,Depth::F32,s,ramp,&packed8,&packed16))return 2;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   Pixel32 p{};std::memcpy(&p,output.data()+(size_t)y*(W*16+PAD)+(size_t)x*16,16);
   if(!std::isfinite(p.a)||!std::isfinite(p.r)||!std::isfinite(p.g)||!std::isfinite(p.b)){
    std::cerr<<"non-finite output mode "<<int(shape)<<" ("<<x<<","<<y<<")\n";return 3;
   }
  }
 }
 // Explicitly invalid pitches must be rejected without modifying destination.
 auto badIn=in;badIn.rowBytes=W*16-1;
 auto before=output;
 if(render(badIn,out,Depth::F32,s,ramp,&packed8,&packed16)){std::cerr<<"Bad src pitch accepted\n";return 4;}
 if(before!=output){std::cerr<<"Invalid pitch modified output\n";return 5;}
 auto badOut=out;badOut.rowBytes=1;
 if(render(in,badOut,Depth::F32,s,ramp,&packed8,&packed16)){std::cerr<<"Bad dst pitch accepted\n";return 6;}
 // Negative rowbytes allowed, bottom-up buffers with the row-zero pointer at the last physical row.
 constexpr int PW=23,PH=11;
 std::array<Pixel8,PW*PH> physical{},reverse{},positive{},negative{};
 for(int y=0;y<PH;y++)for(int x=0;x<PW;x++){
  Pixel8 p{uint8_t(50+(x*7+y*13)%206),uint8_t((x*11)%255),uint8_t((y*17)%255),83};
  physical[y*PW+x]=p;reverse[(PH-1-y)*PW+x]=p;
 }
 Buffer pin{(uint8_t*)reverse.data(),PW,PH,PW*4,0,0};
 Buffer nin{(uint8_t*)(physical.data()+(PH-1)*PW),PW,PH,-PW*4,0,0};
 WritableBuffer pout{(uint8_t*)positive.data(),PW,PH,PW*4,0,0};
 WritableBuffer nout{(uint8_t*)(negative.data()+(PH-1)*PW),PW,PH,-PW*4,0,0};
 s.shape=Shape::Mapping;s.startX=0;s.endX=PW;s.startY=0;s.endY=0;
 if(!render(pin,pout,Depth::U8,s,ramp,&packed8,&packed16)||
    !render(nin,nout,Depth::U8,s,ramp,&packed8,&packed16))return 7;
 for(int y=0;y<PH;y++)for(int x=0;x<PW;x++){
  const Pixel8&a=positive[y*PW+x],&b=negative[(PH-1-y)*PW+x];
  if(std::memcmp(&a,&b,4)!=0){std::cerr<<"negative pitch mismatch\n";return 8;}
 }
 std::cout<<"Stability: NaN/Inf 32bpc, malformed strides, negative rowbytes PASS\n";
}
