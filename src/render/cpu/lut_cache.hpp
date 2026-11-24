#pragma once
// Gradation's bounded, MFR-safe shared LUT cache.
// Key contains ONLY the stop positions/RGBA and interpolation space, not
// shape, geometry, time, UI selection or param IDs.
// A limited number of immutable LUTs is shared across all instances and
// renderer workers. No AE suites or host objects are stored in this cache.
#include "raster_core.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gradation::lutcache {
constexpr uint32_t MaxStops=32;
constexpr size_t MaxEntries=64;
struct Bits {
 uint32_t pos=0,r=0,g=0,b=0,a=0;
};
static_assert(sizeof(Bits)==20);
inline uint32_t bit(float f) noexcept {
 uint32_t u{};
 std::memcpy(&u,&f,sizeof(u));
 return u;
}
inline float unbit(uint32_t u) noexcept {
 float f{};
 std::memcpy(&f,&u,sizeof(f));
 return f;
}
struct Key {
 uint32_t space=0,count=0;
 std::array<Bits,MaxStops> stops{};
 bool operator==(const Key& k)const noexcept {
  return space==k.space&&count==k.count&&count<=MaxStops &&
   std::memcmp(stops.data(),k.stops.data(),count*sizeof(Bits))==0;
 }
};
inline Key makeKey(Space sp,const Stop* stops,uint32_t count){
 Key k{};
 k.space=(uint32_t)sp;
 k.count=std::min(count,MaxStops);
 for(uint32_t i=0;i<k.count;i++){
  const Stop& s=stops[i];
  k.stops[i]={bit(s.position),bit(s.color.r),bit(s.color.g),
              bit(s.color.b),bit(s.color.a)};
 }
 // A constant straight-sRGB ramp is mathematically independent of stop
 // positions and counts. Canonicalize its cache key so 10+ independently
 // keyframed Gradation instances share one immutable LUT instead of
 // rebuilding it on every point move. Never do this in non-sRGB spaces.
 if(k.space==(uint32_t)Space::SRGB&&k.count>=2){
  bool uniform=true;
  const Bits& first=k.stops[0];
  for(uint32_t i=1;i<k.count;i++){
   const Bits& c=k.stops[i];
   if(c.r!=first.r||c.g!=first.g||c.b!=first.b||c.a!=first.a){
    uniform=false;break;
   }
  }
  if(uniform){
   k.count=2;
   k.stops[0].pos=bit(0.f);
   k.stops[1]=k.stops[0];
   k.stops[1].pos=bit(1.f);
  }
 }
 return k;
}
struct KeyHash {
 size_t operator()(const Key& k)const noexcept {
  uint64_t v=14695981039346656037ull;
  auto mix=[&](uint32_t x){v^=x;v*=1099511628211ull;};
  mix(k.space);mix(k.count);
  for(uint32_t i=0;i<std::min(k.count,MaxStops);i++){
   const Bits& b=k.stops[i];
   mix(b.pos);mix(b.r);mix(b.g);mix(b.b);mix(b.a);
  }
  return (size_t)v;
 }
};
struct Entry {
 const Key key;
 // Heavy 4097×RGBA float storage is lazily allocated ONLY if needed.
 // A keyframed 32bpc direct-ramp render must not allocate and initialize
 // 64 KiB for every new stop color/alpha keyframe.
 std::unique_ptr<raster::Ramp> ramp;
 std::unique_ptr<raster::PackedRamp8> packed8;
 std::unique_ptr<raster::PackedRamp16> packed16;
 std::once_flag rampOnce,packed8Once,packed16Once;
 std::atomic<uint32_t> rampBuilds{0};
 explicit Entry(Key k):key(std::move(k)){}
 void prepare() {
  std::call_once(rampOnce,[this](){
   auto generated=std::make_unique<raster::Ramp>();
   std::vector<Stop> stops;
   stops.reserve(key.count);
   for(uint32_t i=0;i<key.count;i++){
    const Bits& b=key.stops[i];
    stops.push_back({unbit(b.pos),
       {unbit(b.r),unbit(b.g),unbit(b.b),unbit(b.a)}});
   }
   Settings s{};
   s.space=(Space)key.space;
   Gradient gradient(s,std::move(stops));
   const int stride=s.space==Space::HCT?8:
       (s.space==Space::CIECAM02||s.space==Space::Spectral)?4:1;
   for(int i=0;i<=4096;i+=stride)
    (*generated)[(size_t)i]=gradient.sample(i/4096.f);
   if(stride>1)for(int i=0;i<4096;i+=stride){
    bool boundary=false;
    for(uint32_t k=0;k<key.count;k++){
     const float pos=unbit(key.stops[k].pos)*4096.f;
     if(pos>(float)i&&pos<(float)(i+stride)){boundary=true;break;}
    }
    const Color& a=(*generated)[(size_t)i],&b=(*generated)[(size_t)i+stride];
    for(int j=1;j<stride;j++){
     if(boundary)(*generated)[(size_t)i+j]=gradient.sample((i+j)/4096.f);
     else{
      const float t=(float)j/stride;
      (*generated)[(size_t)i+j]={a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,
                         a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};
     }
    }
   }
   ramp=std::move(generated);
   rampBuilds.fetch_add(1,std::memory_order_relaxed);
  });
 }
 // Only call after prepare(); std::call_once synchronizes preparation.
 // Direct 32bpc worker threads MUST NOT read this mutable pointer while
 // another AE/MFR worker may be preparing the same cache entry.
 const raster::Ramp& preparedRamp() const noexcept {return *ramp;}
 const raster::PackedRamp8* getPacked8(){
  prepare();
  std::call_once(packed8Once,[this](){
   packed8=std::make_unique<raster::PackedRamp8>(
       raster::makePacked8(*ramp));
  });
  return packed8.get();
 }
 const raster::PackedRamp16* getPacked16(){
  prepare();
  std::call_once(packed16Once,[this](){
   packed16=std::make_unique<raster::PackedRamp16>(
       raster::makePacked16(*ramp));
  });
  return packed16.get();
 }
};
class Pool {
 using Items=std::list<std::shared_ptr<Entry>>;
 mutable std::mutex mutex_;
 Items lru_; // most recently accessed at front
 std::unordered_map<Key,Items::iterator,KeyHash> index_;
 std::atomic<uint64_t> creations_{0};
 public:
 std::shared_ptr<Entry> get(const Key& key,bool eagerPrepare=true){
  std::shared_ptr<Entry> e;
  {
   std::lock_guard<std::mutex> guard(mutex_);
   auto it=index_.find(key);
   if(it!=index_.end()){
    lru_.splice(lru_.begin(),lru_,it->second);
    e=*lru_.begin();
   }else{
    e=std::make_shared<Entry>(key);
    lru_.push_front(e);
    try{index_.emplace(e->key,lru_.begin());}
    catch(...){lru_.pop_front();throw;}
    creations_.fetch_add(1,std::memory_order_relaxed);
    if(lru_.size()>MaxEntries){
     auto last=std::prev(lru_.end());
     index_.erase((*last)->key);
     lru_.erase(last);
    }
   }
  }
  // No global lock is held while evaluating Spectral/HCT LUTs, or
  // doing per-depth packing. std::call_once avoids MFR duplicate builds.
  if(eagerPrepare)e->prepare();
  return e;
 }
 size_t size()const {
  std::lock_guard<std::mutex> guard(mutex_);
  return lru_.size();
 }
 uint64_t creations()const noexcept{return creations_.load(std::memory_order_relaxed);}
};
inline Pool& global(){
 static Pool pool;
 return pool;
}
// Truly immutable reference used only by F32 direct-gradient kernels.
// It must not be fetched through a potentially concurrently preparing
// Entry::ramp unique_ptr, which would be a C++ data race under AE MFR.
inline const raster::Ramp& unusedDirectRamp() noexcept {
 static const raster::Ramp empty{};
 return empty;
}
} // namespace gradation::lutcache
