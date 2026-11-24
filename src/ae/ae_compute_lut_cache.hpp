#pragma once
// Official AE Compute Cache, only for expensive color spaces. No host
// object or suite pointer is held across a callback. Snapshot shared
// ownership keeps the immutable value alive after check-in, during a render.
#include "AE_ComputeCacheSuite.h"
#include "../render/cpu/lut_cache.hpp"
namespace gradation::hostcache {
inline constexpr char classId[]="com.gradation.effect.expensive_lut.v1";
struct Options {lutcache::Key key;unsigned fp=0;};
struct Value {Options options;std::shared_ptr<lutcache::Entry> entry;};
inline bool same(const Options& a,const Options& b)noexcept{
 return a.fp==b.fp&&a.key==b.key;
}
inline A_Err generate(AEGP_CCComputeOptionsRefconP p,AEGP_CCComputeKeyP out)noexcept{
 if(!p||!out)return A_Err_GENERIC;
 const auto& o=*static_cast<const Options*>(p);
 if(o.key.count>lutcache::MaxStops)return A_Err_GENERIC;
 uint64_t a=14695981039346656037ull,b=0x9e3779b97f4a7c15ull;
 auto mix=[&](uint32_t x){
  a=(a^x)*1099511628211ull;
  b^=uint64_t(x)+0x9e3779b97f4a7c15ull+(b<<6)+(b>>2);
  b=(b^(b>>27))*0x94d049bb133111ebull;
 };
 mix(o.key.space);mix(o.key.count);mix(o.fp);
 for(unsigned i=0;i<o.key.count;i++){
  const auto& x=o.key.stops[i];mix(x.pos);mix(x.r);mix(x.g);mix(x.b);mix(x.a);
 }
 const uint64_t words[]={a,b};static_assert(sizeof(*out)==sizeof(words));
 std::memcpy(out,words,sizeof(words));return A_Err_NONE;
}
inline A_Err compute(AEGP_CCComputeOptionsRefconP p,AEGP_CCComputeValueRefconP* out)noexcept{
 if(!p||!out)return A_Err_GENERIC;
 *out=nullptr;const auto& o=*static_cast<const Options*>(p);
 if(o.key.count<2||o.key.count>lutcache::MaxStops||o.key.space>uint32_t(Space::Spectral))return A_Err_GENERIC;
#if defined(__AVX2__)
 struct FP{unsigned old=_mm_getcsr();~FP(){_mm_setcsr(old);}} fp;
 _mm_setcsr((_mm_getcsr()&63u)|o.fp);
#endif
 try{
  auto v=std::make_unique<Value>();v->options=o;
  v->entry=std::make_shared<lutcache::Entry>(o.key);
  v->entry->prepare();
  // Account for all memory up front, no unreported lazy growth.
  v->entry->getPacked8();v->entry->getPacked16();
  *out=v.release();return A_Err_NONE;
 }catch(...){return A_Err_ALLOC;}
}
inline size_t bytes(AEGP_CCComputeValueRefconP v)noexcept{
 return v?sizeof(Value)+sizeof(lutcache::Entry)+sizeof(raster::Ramp)+
   sizeof(raster::PackedRamp8)+sizeof(raster::PackedRamp16)+128:0;
}
inline void erase(AEGP_CCComputeValueRefconP v)noexcept{delete static_cast<Value*>(v);}
inline const AEGP_ComputeCacheCallbacks callbacks{generate,compute,bytes,erase};
inline std::atomic<bool> registered{false};
struct Suite {
 SPBasicSuite* basic=nullptr;
 const AEGP_ComputeCacheSuite1* api=nullptr;
 explicit Suite(SPBasicSuite* b):basic(b){
  if(!b||!b->AcquireSuite||!b->ReleaseSuite)return;
  const void* raw=nullptr;
  if(b->AcquireSuite(kAEGPComputeCacheSuite,kAEGPComputeCacheSuiteVersion1,&raw)==kSPNoError)
   api=static_cast<const AEGP_ComputeCacheSuite1*>(raw);
 }
 ~Suite(){if(api)basic->ReleaseSuite(kAEGPComputeCacheSuite,kAEGPComputeCacheSuiteVersion1);}
};
inline void setup(SPBasicSuite* basic)noexcept{
 if(registered.load(std::memory_order_acquire))return;
 Suite suite(basic);
 if(suite.api&&suite.api->AEGP_ClassRegister&&suite.api->AEGP_ClassUnregister&&
    suite.api->AEGP_ComputeIfNeededAndCheckout&&suite.api->AEGP_GetReceiptComputeValue&&suite.api->AEGP_CheckinComputeReceipt&&
    suite.api->AEGP_ClassRegister(classId,&callbacks)==A_Err_NONE)
  registered.store(true,std::memory_order_release);
}
inline void setdown(SPBasicSuite* basic)noexcept{
 if(!registered.load(std::memory_order_acquire))return;
 Suite suite(basic);
 if(suite.api&&suite.api->AEGP_ClassUnregister&&suite.api->AEGP_ClassUnregister(classId)==A_Err_NONE)
  registered.store(false,std::memory_order_release);
}
inline std::shared_ptr<lutcache::Entry> get(SPBasicSuite* basic,const lutcache::Key& key,bool eager){
 if(eager&&registered.load(std::memory_order_acquire)&&
    (key.space==uint32_t(Space::HCT)||key.space==uint32_t(Space::Spectral)||key.space==uint32_t(Space::CIECAM02))){
  Suite suite(basic);
  if(suite.api&&suite.api->AEGP_ComputeIfNeededAndCheckout&&suite.api->AEGP_GetReceiptComputeValue&&suite.api->AEGP_CheckinComputeReceipt){
   Options o{key,0};
#if defined(__AVX2__)
   o.fp=_mm_getcsr()&0xffc0u;
#endif
   AEGP_CCCheckoutReceiptP receipt=nullptr;
   if(suite.api->AEGP_ComputeIfNeededAndCheckout(classId,&o,false,&receipt)==A_Err_NONE&&receipt){
    struct Checkin{const AEGP_ComputeCacheSuite1* api;AEGP_CCCheckoutReceiptP r;
     ~Checkin(){api->AEGP_CheckinComputeReceipt(r);}} checkin{suite.api,receipt};
    AEGP_CCComputeValueRefconP raw=nullptr;
    if(suite.api->AEGP_GetReceiptComputeValue(receipt,&raw)==A_Err_NONE&&raw){
     const auto& value=*static_cast<const Value*>(raw);
     if(same(value.options,o))return value.entry; // collision-safe
    }
   }
  }
 }
 return lutcache::global().get(key,eager);
}
}
