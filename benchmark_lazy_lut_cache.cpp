#include "lut_cache.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::lutcache;
int main(){
 constexpr int N=240;
 for(int count:{2,3}){
  std::vector<Key> keys;
  keys.reserve(N);
  for(int i=0;i<N;i++){
   const float u=(float)i/N;
   const Stop colors[3]={
    {0,{.15f+u*.6f,.2f,.8f,.3f+u*.5f}},
    {.43f,{.85f,.25f+u*.6f,.18f,.63f}},
    {1,{.93f,.76f-u*.3f,.13f,.95f}}
   };
   const Stop actual[3]={colors[0],count==3?colors[1]:colors[2],colors[2]};
   keys.push_back(makeKey(Space::SRGB,actual,(uint32_t)count));
  }
  auto test=[&](bool eager){
   Pool pool;
   auto begin=std::chrono::steady_clock::now();
   uint64_t builds=0;
   for(const Key& k:keys){
    const auto item=pool.get(k,eager);
    builds+=item->rampBuilds.load();
   }
   const auto end=std::chrono::steady_clock::now();
   return std::pair<double,uint64_t>{
      std::chrono::duration<double,std::milli>(end-begin).count(),builds};
  };
  auto eager=test(true);
  auto lazy=test(false);
  std::cout<<"stop_count "<<count<<" animated_frames "<<N
    <<" EAGER total_ms "<<eager.first<<" LUT builds "<<eager.second
    <<" LAZY total_ms "<<lazy.first<<" LUT builds "<<lazy.second
    <<" pre_render_accel "<<eager.first/lazy.first<<"x\n";
 }
 std::cout<<"sizeof Entry "<<sizeof(Entry)<<" bytes, "
          <<"full float Ramp "<<sizeof(raster::Ramp)
          <<" bytes, deferred in lazy direct renders\n";
}
