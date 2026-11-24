#pragma once
// Bounded, MFR-friendly intra-frame CPU parallelism for expensive 32bpc
// full-frame tiles. This uses only standard C++ threads, not AE private APIs.
// No global pool, callback into AE, persistent threads or image allocations.
// Any AE/MFR render may run concurrently; ONLY ONE large render at a time
// claims extra cores, while all other renders continue safely serially.
#include "raster_core.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <thread>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace gradation::raster::parallel {
inline std::atomic_flag active=ATOMIC_FLAG_INIT;
inline std::atomic<unsigned> renderCalls{0};
inline std::atomic<unsigned> hostCalls{0};
// Cover all production CPU paths, including cache lookup and horizontal
// fast paths which bypass parallel::render(). No host SDK calls here.
struct HostRenderScope {
 HostRenderScope() noexcept {hostCalls.fetch_add(1,std::memory_order_acq_rel);}
 ~HostRenderScope(){hostCalls.fetch_sub(1,std::memory_order_acq_rel);}
 HostRenderScope(const HostRenderScope&)=delete;
 HostRenderScope& operator=(const HostRenderScope&)=delete;
};
inline bool enabled() noexcept {
 static const bool yes=[]{
  const char* value=std::getenv("GRADATION_DISABLE_PARALLEL");
  return !(value&&value[0]=='1');
 }();
 return yes;
}
inline bool worthParallelizing(const WritableBuffer& dst,Depth depth,
 const Settings& settings,blend::Config mode) noexcept {
 if(dst.height<384||dst.width<512)return false;
 if((uint64_t)(unsigned)dst.width*(uint64_t)(unsigned)dst.height<1048576ull)
  return false;
 // Bandwidth-bound Replace generators and fast SourceAlpha Multiply
 // already run near the memory limit. On Linear/Radial/Mapping their
 // parallel gain is only ~1.0-1.3x, too small to justify stealing cores
 // from AE's MFR, UI and other effects. Angle is compute-heavy.
 if(mode.color==blend::Mode::Replace&&
    mode.alpha==blend::Mode::Replace&&settings.shape==Shape::Linear){
  if(settings.endY==settings.startY)return depth==Depth::F32;
  // U16 vertical constant-row NT fill is faster serially on the
  // measured host: avoid intra-frame worker and cache contention.
  if(settings.endX==settings.startX&&depth==Depth::U16)return false;
 }
 // Simple horizontal source-alpha rows retain their dedicated fast path.
 // Every other full frame is eligible for bounded compute parallelism.
 if(depth==Depth::U8&&mode.legacyFast()&&settings.shape==Shape::Linear)
  return (settings.endY==settings.startY||settings.endX==settings.startX)&&dst.width%8==0&&dst.width<=4096;
 return true;
}
inline int configuredWorkers() noexcept {
 // Hyperthreading is less effective than separate cores. Avoid using
 // every physical core so AE retains capacity for UI, MFR and other effects.
 static const int count=[]{
  const unsigned logical=std::thread::hardware_concurrency();
  // Reproducible tuning override. Still bounded to the six-slot
  // worker ABI and never more than available logical CPUs.
  if(const char* value=std::getenv("GRADATION_CPU_WORKERS")){
   const int requested=std::atoi(value);
   if(requested>=1&&requested<=6)
    return std::min(requested,int(std::max(1u,logical)));
  }
  if(logical<6)return 1;
  // Low-impact default: at most one additional OS worker. Higher counts
  // are explicit diagnostic/throughput opt-ins, not the default.
  return 2;
 }();
 return count;
}
// Windows owns the worker threads. Every work item is joined and closed
// before returning, so neither stack pointers nor plugin code survive
// a render callback. No busy wait or persistent plugin-owned workers.
inline bool poolEnabled() noexcept {
 static const bool yes=[]{
  const char* v=std::getenv("GRADATION_DISABLE_SYSTEM_POOL");
  return !(v&&v[0]=='1');
 }();
 return yes;
}
inline bool mayAddWorkers() noexcept {
 return enabled()&&poolEnabled()&&
   hostCalls.load(std::memory_order_acquire)<=1&&
   renderCalls.load(std::memory_order_acquire)<=1;
}
template<class Fn> inline void runStripes(int n,Fn& fn) noexcept {
 if(n<1)return;
 if(n>6||!mayAddWorkers()){
  for(int i=0;i<n;++i)fn(i);
  return;
 }
#if defined(_WIN32)
 if(poolEnabled()){
  struct Job{
   Fn* fn=nullptr;int index=0;
#if defined(__AVX2__)
   unsigned mxcsr=0;
#endif
   static void CALLBACK call(PTP_CALLBACK_INSTANCE,void* context,PTP_WORK) noexcept {
    auto& job=*static_cast<Job*>(context);
#if defined(__AVX2__)
    const auto previous=_mm_getcsr();_mm_setcsr(job.mxcsr);
#endif
    (*job.fn)(job.index);
#if defined(__AVX2__)
    _mm_setcsr(previous);
#endif
   }
  };
  std::array<Job,5> jobs{};
  std::array<PTP_WORK,5> work{};
  for(int i=1;i<n;i++){
   auto& j=jobs[(size_t)i-1];j.fn=&fn;j.index=i;
#if defined(__AVX2__)
   j.mxcsr=_mm_getcsr();
#endif
   auto& w=work[(size_t)i-1];w=CreateThreadpoolWork(&Job::call,&j,nullptr);
   if(w)SubmitThreadpoolWork(w);else fn(i);
  }
  fn(0);
  for(int i=1;i<n;i++){
   auto w=work[(size_t)i-1];
   if(w){WaitForThreadpoolWorkCallbacks(w,FALSE);CloseThreadpoolWork(w);}
  }
  return;
 }
 // Windows always falls back to the calling thread; never create a
 // second threading system when the OS pool is disabled.
 for(int i=0;i<n;++i)fn(i);
 return;
#endif
#if defined(__APPLE__)
 dispatch_apply_f(static_cast<size_t>(n),dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),
                  &fn,[](void* context,size_t i){(*static_cast<Fn*>(context))(int(i));});
 return;
#endif
 std::array<std::thread,5> extra;
 int created=0;
 for(int i=1;i<n;i++){
  try{extra[(size_t)created]=std::thread(fn,i);created++;}
  catch(...){for(int j=i;j<n;j++)fn(j);break;}
 }
 fn(0);
 for(int i=0;i<created;i++)extra[(size_t)i].join();
}
inline bool render(const Buffer& src,const WritableBuffer& dst,Depth depth,
 const Settings& settings,const Ramp& ramp,const PackedRamp8* p8,
 const PackedRamp16* p16,blend::Config cfg,
 const DirectRamp* direct=nullptr) noexcept {
 auto serial=[&]{
  return gradation::raster::render(src,dst,depth,settings,
                                  ramp,p8,p16,cfg,direct);
 };
 const unsigned prior=renderCalls.fetch_add(1,std::memory_order_acq_rel);
 struct RenderExit { ~RenderExit(){renderCalls.fetch_sub(1,std::memory_order_acq_rel);} } renderExit;
 // Do not add parallel work when the host is already rendering concurrently.
 if(prior!=0||!mayAddWorkers()||!worthParallelizing(dst,depth,settings,cfg)||
    (src.data&&src.data==dst.data))
  return serial();
 // Angle math scales to six physical workers; other shapes generally
 // saturate memory bandwidth by four, leaving more cores for AE/MFR.
 // Measured compute-heavy graphs benefit from six workers; simple
 // bandwidth-bound generation keeps the original four-worker limit.
 static const bool extraComposite=[](){
  const char* v=std::getenv("GRADATION_CPU_COMPOSITE_WORKERS");
  return !(v&&v[0]=='0');
 }();
 const bool complex=!cfg.legacyFast()&&
   !(cfg.color==blend::Mode::Replace&&cfg.alpha==blend::Mode::Replace);
 const int n=std::min(settings.shape==Shape::Angle||(complex&&extraComposite)?
                      configuredWorkers():std::min(4,configuredWorkers()),dst.height);
 #if defined(_WIN32)
 // Disabling the system pool must not silently create raw threads.
 if(!poolEnabled())return serial();
 #endif
 if(n<=1)return serial();
 if(active.test_and_set(std::memory_order_acquire))
  return serial(); // Other MFR/FX renders never wait for us.
 struct Release {
  ~Release(){active.clear(std::memory_order_release);}
 } release;
 std::atomic<bool> okay{true};
 auto doStripe=[&](int i)noexcept{
  const int from=(int)((int64_t)i*dst.height/n);
  const int to=(int)((int64_t)(i+1)*dst.height/n);
  WritableBuffer tile{
    dst.data+(std::ptrdiff_t)from*dst.rowBytes,dst.width,to-from,
    dst.rowBytes,dst.originX,dst.originY+from
  };
  try{
   if(!gradation::raster::render(src,tile,depth,settings,ramp,
                                p8,p16,cfg,direct))
     okay.store(false,std::memory_order_relaxed);
  }catch(...){
   okay.store(false,std::memory_order_relaxed);
  }
 };
 runStripes(n,doStripe);
 return okay.load(std::memory_order_relaxed);
}
} // namespace gradation::raster::parallel
