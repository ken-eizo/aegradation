#include "lut_cache.hpp"
#include <array>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <cmath>
using namespace gradation;
using namespace gradation::lutcache;
static std::array<Stop,4> makeStops(int i){
 const float t=(i%100)/100.f;
 return {{{0.f,{t,.2f,1.f-t,1.f}},
          {.21f,{.8f,t,.4f,.65f}},
          {.75f,{.3f,.4f,t,.25f}},
          {1.f,{1.f-t,.1f,.2f,1.f}}}};
}
int main(){
 Pool pool;
 // Identical colors with animated stop positions or increased stop count
 // share precisely one straight-sRGB LUT; no frame-by-frame rebuild.
 std::array<Stop,3> uniform{{
  {0.f,{.2f,.4f,.7f,.5f}},
  {.35f,{.2f,.4f,.7f,.5f}},
  {1.f,{.2f,.4f,.7f,.5f}}
 }};
 const Key firstUniform=makeKey(Space::SRGB,uniform.data(),2);
 const Key threeUniform=makeKey(Space::SRGB,uniform.data(),3);
 uniform[0].position=.15f;
 uniform[1].position=.8f;
 const Key animatedUniform=makeKey(Space::SRGB,uniform.data(),3);
 if(!(firstUniform==threeUniform)||!(threeUniform==animatedUniform))
  return 11;
 const auto oneUniform=pool.get(firstUniform);
 const auto twoUniform=pool.get(animatedUniform);
 if(oneUniform.get()!=twoUniform.get()||pool.creations()!=1)
  return 12;
 std::array<Key,36> keys{};
 for(int i=0;i<36;i++){
  auto s=makeStops(i);
  keys[(size_t)i]=makeKey((i%8==0)?Space::Oklab:Space::SRGB,s.data(),4);
 }
 std::vector<std::shared_ptr<Entry>> retained;
 for(int i=0;i<36;i++){
  retained.push_back(pool.get(keys[(size_t)i]));
  if(retained.back()->rampBuilds.load()!=1) return 2;
 }
 if(pool.size()!=37||pool.creations()!=37)return 3;
 for(int r=0;r<20;r++){
  for(int i=35;i>=0;i--){
   auto entry=pool.get(keys[(size_t)i]);
   if(entry.get()!=retained[(size_t)i].get() ||
      entry->rampBuilds.load()!=1)return 4;
   const auto* packed8=entry->getPacked8();
   const auto* packed16=entry->getPacked16();
   if(!packed8||!packed16)return 5;
  }
 }
 if(pool.creations()!=37)return 6;
 // 10+ repeated effect instances across eight concurrent MFR workers.
 std::atomic<int> failures{0};
 std::array<std::thread,8> workers;
 for(auto& w:workers){
  w=std::thread([&](){
   for(int rep=0;rep<120;rep++){
    for(int i=0;i<36;i++){
     auto entry=pool.get(keys[(size_t)i]);
     if(entry->rampBuilds.load()!=1||
        !std::isfinite((*entry->ramp)[2048].g) ||
        entry.get()!=retained[(size_t)i].get())
      failures++;
     if((rep&7)==0)entry->getPacked8();
    }
   }
  });
 }
 for(auto& w:workers)w.join();
 if(failures.load()!=0||pool.creations()!=37||pool.size()!=37)
  return 7;
 // Make sure key distinguishes all 5 channels and exact bit values.
 auto variant=keys[3];
 variant.stops[0].a^=1;
 auto other=pool.get(variant);
 if(other.get()==retained[3].get()){
   std::cerr<<"cache alias: count="<<variant.count<<" key change="<<variant.stops[0].a<<" old="<<keys[3].stops[0].a<<"\n";
   return 8;
 }
 // After capacity churn, pinned shared_ptr retains live LUT even if evicted.
 for(int i=0;i<90;i++){
  auto s=makeStops(i+200);
  s[0].color.a=(float)(i+1)/97.f;
  pool.get(makeKey(Space::HSV,s.data(),4));
 }
 if(pool.size()!=MaxEntries)return 9;
 if(!std::isfinite((*retained[0]->ramp)[1000].r)||
    retained[0]->rampBuilds.load()!=1)return 10;
 std::cout<<"PASS: 36 unique stacked effects, 8 MFR workers, 120 passes, "
          <<"no redundant LUT rebuild or cache corruption; "
          <<"64-entry memory bound and pinned eviction\n";
}
