#include "raster_core.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
constexpr int N=12,FRAMES=16,W=128,H=64;
struct Work {
 int depth=0;
 Settings s{};
 blend::Config cfg{};
 DirectRamp direct{};
 Ramp ramp{};
 PackedRamp8 p8{};
 PackedRamp16 p16{};
 std::vector<uint8_t> source;
 std::array<std::vector<uint8_t>,FRAMES> expected{};
 std::array<Settings,FRAMES> settings{};
 std::vector<uint8_t> output;
};
int main(){
 std::vector<std::unique_ptr<Work>> items;
 items.reserve(N);
 for(int i=0;i<N;i++){
  auto w=std::make_unique<Work>();
  w->depth=i%3;
  const int bpp=w->depth==0?4:w->depth==1?8:16;
  w->s.shape=(Shape)(i%4);
  w->s.startX=13.f+i*3.5f;
  w->s.startY=11.f+i*2.25f;
  w->s.endX=w->s.startX+42.5f;
  w->s.endY=w->s.startY+25.2f;
  if(i%5==0)w->s.invert=true;
  const blend::Config modes[]={
   {blend::Mode::Replace,blend::Mode::Replace,0},
   {blend::Mode::Replace,blend::Mode::Multiply,0},
   {blend::Mode::Normal,blend::Mode::Multiply,0},
   {blend::Mode::Screen,blend::Mode::Normal,0}};
  w->cfg=modes[i%4];
  w->direct.first={.1f+i*.02f,.2f+i*.03f,.35f,.2f};
  w->direct.last={.9f-i*.02f,.55f,.1f+i*.03f,.98f};
  Gradient g(w->s,{{0,w->direct.first},{1,w->direct.last}});
  for(int k=0;k<=4096;k++)w->ramp[k]=g.sample(k/4096.f);
  w->p8=makePacked8(w->ramp);
  w->p16=makePacked16(w->ramp);
  w->source.resize((size_t)W*H*bpp);
  w->output.resize(w->source.size());
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){
   const float a=((x+y)%7==0)?.4f:1.f;
   uint8_t* p=w->source.data()+((size_t)y*W+x)*bpp;
   if(w->depth==0){
    Pixel8 v{u8(a),u8(a*.2f),u8(a*.5f),u8(a*.8f)};
    std::memcpy(p,&v,4);
   }else if(w->depth==1){
    Pixel16 v{(uint16_t)(a*32768),(uint16_t)(a*.2f*32768),
              (uint16_t)(a*.5f*32768),(uint16_t)(a*.8f*32768)};
    std::memcpy(p,&v,8);
   }else{
    Pixel32 v{a,a*.2f,a*.5f,a*.8f};std::memcpy(p,&v,16);
   }
  }
  const Depth dep=(Depth)w->depth;
  for(int f=0;f<FRAMES;f++){
   Settings s=w->s;
   s.startX+=(float)f*.125f;
   s.endY+=(float)f*.15f;
   w->settings[(size_t)f]=s;
   auto& expected=w->expected[(size_t)f];
   expected.resize(w->source.size());
   Buffer src{w->source.data(),W,H,W*bpp,0,0};
   if(w->cfg.alpha==blend::Mode::Replace&&
      w->cfg.color==blend::Mode::Replace&&s.shape!=Shape::Mapping)
       src=Buffer{};
   WritableBuffer dst{expected.data(),W,H,W*bpp,0,0};
   const DirectRamp* dr=w->depth==0&&w->cfg.color!=blend::Mode::Normal?
                        nullptr:&w->direct;
   if(!render(src,dst,dep,s,w->ramp,&w->p8,&w->p16,w->cfg,dr))
    return 2;
  }
  items.push_back(std::move(w));
 }
 std::atomic<int> failures{0};
 std::array<std::thread,N> workers;
 for(int i=0;i<N;i++){
  workers[(size_t)i]=std::thread([&,i](){
   Work& w=*items[(size_t)i];
   const int bpp=w.depth==0?4:w.depth==1?8:16;
   const Depth depth=(Depth)w.depth;
   const DirectRamp* direct=w.depth==0&&w.cfg.color!=blend::Mode::Normal?
                            nullptr:&w.direct;
   for(int f=0;f<FRAMES;f++){
    Buffer src{w.source.data(),W,H,W*bpp,0,0};
    if(w.cfg.alpha==blend::Mode::Replace&&
       w.cfg.color==blend::Mode::Replace&&w.s.shape!=Shape::Mapping)
      src=Buffer{};
    WritableBuffer output{w.output.data(),W,H,W*bpp,0,0};
    if(!render(src,output,depth,w.settings[(size_t)f],
               w.ramp,&w.p8,&w.p16,w.cfg,direct) ||
       std::memcmp(w.output.data(),w.expected[(size_t)f].data(),
                   w.output.size())!=0)
      failures.fetch_add(1,std::memory_order_relaxed);
   }
  });
 }
 for(auto& w:workers)w.join();
 if(failures.load()){
  std::cerr<<"Concurrent mismatch count "<<failures.load()<<"\n";
  return 3;
 }
 std::cout<<"PASS: 12 different instances x 16 frames x 8/16/32bpc, "
    <<"4 gradient types, independent gradient stops and blends, "
    <<"concurrent MFR pixel-exact vs serialized\n";
}
