#include "../../src/render/cpu/lut_cache.hpp"
#include "../../src/render/cpu/raster_core.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 constexpr int W=161,H=93,INSTANCES=12,FRAMES=80;
 const Stop stops[3]={{0,{.12f,.81f,.32f,.33f}},
                     {.43f,{.91f,.23f,.64f,.79f}},
                     {1,{.93f,.76f,.16f,.95f}}};
 DirectRamp direct{stops[0].color,stops[2].color,
                   stops[1].color,.43f,3};
 Gradient grad({},std::vector<Stop>(std::begin(stops),std::end(stops)));
 Ramp prepared{};
 for(int k=0;k<=4096;k++)prepared[(size_t)k]=grad.sample(k/4096.f);
 lutcache::Pool pool;
 const auto key=lutcache::makeKey(Space::SRGB,stops,3);
 const auto entry=pool.get(key,false);
 if(!entry||entry->rampBuilds.load()!=0)return 1;
 std::vector<uint8_t> source((size_t)W*H*16);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  const float a=(x+y)%7==0?.24f:1.f;
  const Pixel32 p{a,a*.35f,a*.63f,a*.78f};
  std::memcpy(source.data()+((size_t)y*W+x)*16,&p,16);
 }
 Buffer in{source.data(),W,H,W*16,0,0};
 std::array<std::vector<uint8_t>,INSTANCES> expected{};
 std::array<Settings,INSTANCES> geometry{};
 for(int i=0;i<INSTANCES;i++){
  Settings s{};
  s.shape=(Shape)(i%4);
  s.startX=32.f+i;s.endX=139.f+i;
  s.startY=12.f+i*.5f;s.endY=76.f+i*.25f;
  geometry[(size_t)i]=s;
  expected[(size_t)i].resize(source.size());
  WritableBuffer dst{expected[(size_t)i].data(),W,H,W*16,0,0};
  const blend::Config c{blend::Mode::Normal,blend::Mode::Multiply,0};
  if(!render(in,dst,Depth::F32,s,prepared,nullptr,nullptr,c,&direct))
   return 2;
 }
 std::atomic<bool> begin{false};
 std::atomic<int> errors{0};
 std::array<std::thread,INSTANCES> workers{};
 for(int i=0;i<INSTANCES;i++)workers[(size_t)i]=std::thread([&,i]{
  std::vector<uint8_t> current(source.size());
  WritableBuffer dst{current.data(),W,H,W*16,0,0};
  while(!begin.load(std::memory_order_acquire))
   std::this_thread::yield();
  for(int f=0;f<FRAMES;f++){
   // Simulate another AE frame requesting the same cache key but at a
   // different effect bit depth while direct F32 renderers are active.
   if(i==0 && f==10)entry->prepare();
   auto other=pool.get(key,false);
   if(other.get()!=entry.get())errors.fetch_add(1);
   const blend::Config c{blend::Mode::Normal,blend::Mode::Multiply,0};
   const bool okay=render(in,dst,Depth::F32,geometry[(size_t)i],
     lutcache::unusedDirectRamp(),nullptr,nullptr,c,&direct);
   if(!okay||std::memcmp(current.data(),
       expected[(size_t)i].data(),current.size())!=0)
    errors.fetch_add(1);
  }
 });
 begin.store(true,std::memory_order_release);
 for(auto& t:workers)t.join();
 if(errors.load()!=0||entry->rampBuilds.load()!=1){
  std::cerr<<"FAIL concurrent F32 direct + late LUT preparation: "
   <<errors.load()<<" errors, builds "<<entry->rampBuilds.load()<<"\n";
  return 3;
 }
 std::cout<<"PASS "<<INSTANCES<<" concurrent MFR-like render instances x "
   <<FRAMES<<" frames, identical output during shared cache 0->1 lazy LUT "
   <<"transition, entry built exactly once\n";
}
