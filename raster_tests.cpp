#include "raster_core.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int SW=64,SH=40,DW=40,DH=24;
 for(int shape=0;shape<4;shape++){
  Settings s;
  s.shape=(Shape)shape;
  s.startX=9;s.startY=7;s.endX=55;s.endY=12;s.space=Space::Spectral;
  Gradient g(s,{{0,{1,0,0,1}},{.4f,{0,1,0,.7f}},{1,{0,0,1,1}}});
  Ramp ramp;
  for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
  auto packed=makePacked8(ramp);auto packed16=makePacked16(ramp);
  std::vector<Pixel8> src(SW*SH),out(DW*DH);
  for(int y=0;y<SH;y++)for(int x=0;x<SW;x++)
   src[y*SW+x]={uint8_t((x%4==0)?128:255),160,80,40};
  Buffer input{(uint8_t*)src.data(),SW,SH,SW*4,0,0};
  WritableBuffer dst{(uint8_t*)out.data(),DW,DH,DW*4,8,5};
  render(input,dst,Depth::U8,s,ramp,&packed,&packed16);
  for(int y=0;y<DH;y++)for(int x=0;x<DW;x++){
   const Pixel8& a=out[y*DW+x];
   const Pixel8& orig=src[(y+5)*SW+(x+8)];
   float t=g.coordinate((float)(x+8),(float)(y+5),
     Ops<Pixel8>::luma(orig));
   auto expect=g.sample(t);
   float alpha=expect.a*orig.a/255.f;
   int dr=std::abs((int)a.r-(int)u8(expect.r*alpha));
   int dg=std::abs((int)a.g-(int)u8(expect.g*alpha));
   int db=std::abs((int)a.b-(int)u8(expect.b*alpha));
   if(dr>3||dg>3||db>3){std::cerr<<"color mismatch shape "<<shape<<" x "<<x<<" y "<<y<<" diff "<<dr<<" "<<dg<<" "<<db<<"\n";return 2;}
   if(std::abs((int)a.a-(int)u8(alpha))>2){std::cerr<<"alpha mismatch\n";return 3;}
  }
 }
 double maxErr=0;
 for(int i=-3600;i<=3600;i++){
  float a=i*.001f;
  float x=std::cos(a),y=std::sin(a);
  maxErr=std::max(maxErr,(double)std::abs(fastAtan2(y,x)-std::atan2(y,x)));
 }
 if(maxErr>1.e-5){std::cerr<<"atan error "<<maxErr<<"\n";return 4;}
 // Reversed shapes, non-zero row padding and origins exercise 16- and 32-bit.
 for(int depth: {16,32}){
  constexpr int W=80,H=48,PAD=8;
  const int bytesPerPixel=depth==16?8:16;
  std::vector<uint8_t> ib((size_t)(W*bytesPerPixel+PAD)*H,0);
  std::vector<uint8_t> ob((size_t)(W*bytesPerPixel+PAD)*H,0);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   if(depth==16){
    Pixel16 p{32768,20000,20000,20000};std::memcpy(ib.data()+y*(W*8+PAD)+x*8,&p,8);
   }else{
    Pixel32 p{1,.75f,.5f,.25f};std::memcpy(ib.data()+y*(W*16+PAD)+x*16,&p,16);
   }
  }
  Settings s;s.shape=Shape::Radial;s.startX=0;s.startY=0;s.endX=W;s.endY=0;
  Gradient g(s);
  Ramp ramp;for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
  auto packed=makePacked8(ramp);auto packed16=makePacked16(ramp);
  Buffer in{ib.data(),W,H,W*bytesPerPixel+PAD,0,0};
  WritableBuffer out{ob.data(),W,H,W*bytesPerPixel+PAD,0,0};
  render(in,out,depth==16?Depth::U16:Depth::F32,s,ramp,&packed,&packed16);
  if(depth==16){Pixel16 p{};std::memcpy(&p,ob.data(),8);assert(p.a==32768&&p.r>32000);}
  else{Pixel32 p{};std::memcpy(&p,ob.data(),16);assert(p.a>.99&&p.r>.99);}
 }
 std::cout<<"Raster: 4 shapes at 8bpc + 16/32bpc padded buffers + angle max error "<<maxErr<<" rad: PASS\n";
}
