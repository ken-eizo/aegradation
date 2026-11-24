#include "raster_core.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
static int errors=0;
static void check(int actual,int expect,int tolerance,const char* mode,int x,int y,int ch){
 if(std::abs(actual-expect)>tolerance){
  if(errors<15)std::cerr<<mode<<" pixel("<<x<<","<<y<<") ch "<<ch
   <<" actual="<<actual<<" reference="<<expect<<" tolerance="<<tolerance<<"\n";
  errors++;
 }
}
static float coord(const Settings& s,int x,int y,float lum){
 const float dx=s.endX-s.startX,dy=s.endY-s.startY;
 const float length=std::max(1e-6f,std::sqrt(dx*dx+dy*dy));
 float t=0;
 switch(s.shape){
  case Shape::Linear:t=((x-s.startX)*dx+(y-s.startY)*dy)/(length*length);break;
  case Shape::Radial:t=std::hypot(x-s.startX,y-s.startY)/length;break;
  case Shape::Angle:{
   t=(fastAtan2(y-s.startY,x-s.startX)-fastAtan2(dy,dx))/(6.28318530718f);
   t-=std::floor(t);break;
  }
  case Shape::Mapping:t=lum;break;
 }
 return transform(t,100.f/std::max(s.scalePercent,.0001f),
                  s.offsetPx/length,s.loop,s.reflect,s.invert);
}
int main(){
 constexpr int W=131,H=37,PAD=48;
 for(int depth:{8,16,32})for(int mode=0;mode<4;mode++)for(int opaque=0;opaque<2;opaque++){
  const int bpp=depth==8?4:(depth==16?8:16);
  const int row=(W*bpp+PAD);
  std::vector<uint8_t> input((size_t)row*H),output((size_t)row*H);
  Settings s;s.shape=(Shape)mode;
  s.startX=14;s.startY=3;s.endX=100;s.endY=22;
  s.offsetPx=4.f;s.scalePercent=92.f;
  Gradient gradient({},{{0,{.95f,.25f,.03f,1}},
                       {.4f,{.1f,.75f,.93f,.78f}},
                       {1,{.2f,.04f,.74f,.52f}}});
  Ramp lut{};for(int i=0;i<=4096;i++)lut[i]=gradient.sample(i/4096.f);
  PackedRamp8 p8=makePacked8(lut);PackedRamp16 p16=makePacked16(lut);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const int a8=opaque?255:((x*37+y*19)%256);
   const float a=a8/255.f;
   const float r=(x*13+y*7)%256/255.f;
   const float g=(x*3+y*17)%256/255.f;
   const float b=(x*19+y*5)%256/255.f;
   uint8_t* p=input.data()+(size_t)y*row+(size_t)x*bpp;
   if(depth==8){
    Pixel8 c{(uint8_t)a8,u8(r*a),u8(g*a),u8(b*a)};
    std::memcpy(p,&c,bpp);
   }else if(depth==16){
    Pixel16 c{(uint16_t)std::lround(a*32768),(uint16_t)std::lround(r*a*32768),
              (uint16_t)std::lround(g*a*32768),(uint16_t)std::lround(b*a*32768)};
    std::memcpy(p,&c,bpp);
   }else{
    Pixel32 c{a,r*a,g*a,b*a};std::memcpy(p,&c,bpp);
   }
  }
  Buffer src{input.data(),W,H,row,0,0};
  WritableBuffer dst{output.data(),W,H,row,0,0};
  render(src,dst,(Depth)(depth==8?0:depth==16?1:2),s,lut,&p8,&p16);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const uint8_t* ptr=input.data()+(size_t)y*row+(size_t)x*bpp;
   const uint8_t* result=output.data()+(size_t)y*row+(size_t)x*bpp;
   float lum=0,a=0;
   if(depth==8){
    Pixel8 in,got;std::memcpy(&in,ptr,bpp);std::memcpy(&got,result,bpp);
    lum=Ops<Pixel8>::luma(in);a=in.a/255.f;
    Color expected=lookup(lut,coord(s,x,y,lum));
    Pixel8 exp=Ops<Pixel8>::output(expected,in);
    check(got.a,exp.a,2,"U8",x,y,0);
    check(got.r,exp.r,2,"U8",x,y,1);
    check(got.g,exp.g,2,"U8",x,y,2);
    check(got.b,exp.b,2,"U8",x,y,3);
   }else if(depth==16){
    Pixel16 in,got;std::memcpy(&in,ptr,bpp);std::memcpy(&got,result,bpp);
    lum=Ops<Pixel16>::luma(in);a=in.a/32768.f;
    Color expected=lookup(lut,coord(s,x,y,lum));
    Pixel16 exp=Ops<Pixel16>::output(expected,in);
    check(got.a,exp.a,3,"U16",x,y,0);
    check(got.r,exp.r,3,"U16",x,y,1);
    check(got.g,exp.g,3,"U16",x,y,2);
    check(got.b,exp.b,3,"U16",x,y,3);
   }else{
    Pixel32 in,got;std::memcpy(&in,ptr,bpp);std::memcpy(&got,result,bpp);
    lum=Ops<Pixel32>::luma(in);a=in.a;
    Color expected=lookup(lut,coord(s,x,y,lum));
    Pixel32 exp=Ops<Pixel32>::output(expected,in);
    auto checkf=[&](float lhs,float rhs,int ch){
     if(std::abs(lhs-rhs)>1.e-3f){
      if(errors<15)std::cerr<<"F32 pixel "<<x<<","<<y<<" ch "<<ch<<" actual "<<lhs<<" expected "<<rhs<<"\n";
      errors++;
     }
    };
    checkf(got.a,exp.a,0);checkf(got.r,exp.r,1);checkf(got.g,exp.g,2);checkf(got.b,exp.b,3);
   }
  }
 }
 std::cout<<"Pixel parity: 3 depths x 4 shapes x opaque/translucent, "<<errors<<" errors\n";
 return errors?1:0;
}
