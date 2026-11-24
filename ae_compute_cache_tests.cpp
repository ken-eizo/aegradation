#include "AEConfig.h"
#include "ae_compute_lut_cache.hpp"
#include <iostream>
using namespace gradation;
using namespace gradation::hostcache;
namespace {
const AEGP_ComputeCacheCallbacks* cb=nullptr;
AEGP_CCComputeValueRefconP cached=nullptr;
AEGP_CCComputeKey last{};
int checks=0,returns=0,builds=0,regs=0,unregs=0;
bool pending=false,collision=false;
A_Err reg(AEGP_CCComputeClassIdP,const AEGP_ComputeCacheCallbacks* c){cb=c;regs++;return A_Err_NONE;}
A_Err unreg(AEGP_CCComputeClassIdP){if(cached)cb->delete_compute_value(cached);cached=nullptr;cb=nullptr;unregs++;return A_Err_NONE;}
A_Err checkout(AEGP_CCComputeClassIdP,AEGP_CCComputeOptionsRefconP options,bool wait,AEGP_CCCheckoutReceiptP* r){
 *r=nullptr;if(wait||pending)return A_Err_GENERIC;
 AEGP_CCComputeKey k{};if(cb->generate_key(options,&k))return A_Err_GENERIC;
 if(!cached||(!collision&&std::memcmp(&last,&k,sizeof(k)))){
  if(cached)cb->delete_compute_value(cached);cached=nullptr;
  if(cb->compute(options,&cached))return A_Err_GENERIC;
  last=k;builds++;
 }
 *r=cached;checks++;return A_Err_NONE;
}
A_Err value(const AEGP_CCCheckoutReceiptP r,AEGP_CCComputeValueRefconP* out){*out=r;return A_Err_NONE;}
A_Err checkin(AEGP_CCCheckoutReceiptP){returns++;return A_Err_NONE;}
AEGP_ComputeCacheSuite1 api{reg,unreg,checkout,nullptr,value,checkin};
SPErr acquire(const char* name,int32 version,const void** out){
 *out=nullptr;if(std::strcmp(name,kAEGPComputeCacheSuite)||version!=kAEGPComputeCacheSuiteVersion1)return kSPBadParameterError;
 *out=&api;return kSPNoError;
}
SPErr release(const char*,int32){return kSPNoError;}
}
int main(){
 SPBasicSuite basic{};basic.AcquireSuite=acquire;basic.ReleaseSuite=release;
 setup(&basic);setup(&basic);
 if(regs!=1||!registered)return 1;
 Stop stops[]={{0,{.2f,.4f,.6f,.4f}},{1,{.8f,.3f,.1f,.95f}}};
 for(Space space:{Space::HCT,Space::Spectral,Space::CIECAM02}){
  auto k=lutcache::makeKey(space,stops,2);
  auto a=get(&basic,k,true),b=get(&basic,k,true);
  if(a!=b||a->rampBuilds!=1||!a->packed8||!a->packed16)return 2;
  auto ref=lutcache::global().get(k);
  if(std::memcmp(a->preparedRamp().data(),ref->preparedRamp().data(),sizeof(raster::Ramp)))return 3;
  if(cb->approx_size_value(cached)<sizeof(raster::Ramp)+sizeof(raster::PackedRamp8)+sizeof(raster::PackedRamp16))return 4;
 }
 auto k=lutcache::makeKey(Space::HCT,stops,2);
 pending=true;auto local=get(&basic,k,true);pending=false;
 if(local!=lutcache::global().get(k))return 5;
 collision=true;auto fallback=get(&basic,k,true);collision=false;
 if(fallback->key.space!=uint32_t(Space::HCT)||fallback!=local)return 6;
 auto held=get(&basic,k,true);
 setdown(&basic);if(unregs!=1||registered||checks!=returns||!held->ramp)return 7;
 if(get(&basic,k,true)!=local)return 8;
 std::cout<<"PASS AE Compute Cache lifecycle, hit, pending fallback, collision rejection, receipt balance, exact LUT, post-purge snapshot; builds="<<builds<<"\n";
}
