#pragma once
#include "AE_EffectGPUSuites.h"
#include "gradation_cuda_params.hpp"
#include "gradation_cuda_blob.hpp"
#include <windows.h>
#include <mutex>
#include <memory>
namespace gradation::cuda_render {
inline bool supports(PF_GPU_Framework f) noexcept {return f==PF_GPU_Framework_CUDA;}
struct Driver {
 HMODULE dll=nullptr;
 using Ptr=void*;
 int (WINAPI *init)(unsigned)=nullptr;
 int (WINAPI *push)(Ptr)=nullptr;
 int (WINAPI *pop)(Ptr*)=nullptr;
 int (WINAPI *load)(Ptr*,const void*)=nullptr;
 int (WINAPI *function)(Ptr*,Ptr,const char*)=nullptr;
 int (WINAPI *unload)(Ptr)=nullptr;
 int (WINAPI *upload)(uint64_t,const void*,size_t,Ptr)=nullptr;
 int (WINAPI *sync)(Ptr)=nullptr;
 int (WINAPI *launch)(Ptr,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,
                      unsigned,Ptr,void**,void**)=nullptr;
 template<class T> bool bind(T& f,const char* name){
  f=reinterpret_cast<T>(GetProcAddress(dll,name));return f!=nullptr;
 }
 bool open(){
  dll=LoadLibraryExW(L"nvcuda.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
  return dll&&bind(init,"cuInit")&&bind(push,"cuCtxPushCurrent_v2")&&
   bind(pop,"cuCtxPopCurrent_v2")&&bind(load,"cuModuleLoadData")&&
   bind(function,"cuModuleGetFunction")&&bind(unload,"cuModuleUnload")&&
   bind(upload,"cuMemcpyHtoDAsync_v2")&&bind(sync,"cuStreamSynchronize")&&
   bind(launch,"cuLaunchKernel")&&init(0)==0;
 }
 ~Driver(){if(dll)FreeLibrary(dll);}
};
struct Context {
 Driver& d; bool ok;
 Context(Driver& a,void* ctx):d(a),ok(ctx&&d.push(ctx)==0){}
 ~Context(){if(ok){void* ignored=nullptr;d.pop(&ignored);}}
};
struct Device {
 Driver d;
 PF_GPUDeviceInfo info{};
 void* module=nullptr;
 void* fast=nullptr;
 void* general=nullptr;
 void* ramp=nullptr;
 void* pinned=nullptr;
 A_u_long index=0;
 std::mutex mutex;
 std::shared_ptr<lutcache::Entry> uploaded;
 static constexpr size_t rampBytes=4097*sizeof(Rgba);
 void dispose(PF_InData* in,const PF_GPUDeviceSuite1* suite){
  Context guard(d,info.contextPV);
  if(!guard.ok)return;
  if(info.command_queuePV)d.sync(info.command_queuePV);
  if(ramp){suite->FreeDeviceMemory(in->effect_ref,index,ramp);ramp=nullptr;}
  if(pinned){suite->FreeHostMemory(in->effect_ref,index,pinned);pinned=nullptr;}
  if(module){d.unload(module);module=nullptr;}
 }
};
inline PF_Err setup(PF_InData* in,PF_OutData* out,PF_GPUDeviceSetupExtra* ex){
 if(!in||!out||!ex||!ex->input||!ex->output)return PF_Err_BAD_CALLBACK_PARAM;
 ex->output->gpu_data=nullptr;
 out->out_flags2&=~PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
 if(ex->input->what_gpu!=PF_GPU_Framework_CUDA||!in->pica_basicP)return PF_Err_NONE;
 wchar_t disable[4]{};
 if(GetEnvironmentVariableW(L"GRADATION_DISABLE_CUDA",disable,4)>0&&disable[0]==L'1')return PF_Err_NONE;
 AEFX_SuiteScoper<PF_GPUDeviceSuite1,true> suite(in,kPFGPUDeviceSuite,kPFGPUDeviceSuiteVersion1,out);
 if(!suite.get())return PF_Err_NONE;
 auto state=std::make_unique<Device>();state->index=ex->input->device_index;
 if(suite->GetDeviceInfo(in->effect_ref,state->index,&state->info)!=PF_Err_NONE||
    !state->info.compatibleB||state->info.device_framework!=PF_GPU_Framework_CUDA||
    !state->info.contextPV||!state->d.open())return PF_Err_NONE;
 Context guard(state->d,state->info.contextPV);
 if(!guard.ok)return PF_Err_NONE;
 if(state->d.load(&state->module,kCudaFatbin)!=0||
    state->d.function(&state->fast,state->module,"gradation_fast")!=0||
    state->d.function(&state->general,state->module,"gradation_general")!=0||
    suite->AllocateDeviceMemory(in->effect_ref,state->index,Device::rampBytes,&state->ramp)!=PF_Err_NONE||
    !state->ramp||
    suite->AllocateHostMemory(in->effect_ref,state->index,Device::rampBytes,&state->pinned)!=PF_Err_NONE||
    !state->pinned){
  state->dispose(in,suite.get());return PF_Err_NONE;
 }
 out->out_flags2|=PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
 ex->output->gpu_data=state.release();
 // guard holds Driver owned by released state; pop happens before return.
 return PF_Err_NONE;
}
inline PF_Err setdown(PF_InData* in,PF_GPUDeviceSetdownExtra* ex){
 if(!in||!ex||!ex->input)return PF_Err_BAD_CALLBACK_PARAM;
 std::unique_ptr<Device> state(static_cast<Device*>(ex->input->gpu_data));
 if(!state)return PF_Err_NONE;
 AEFX_SuiteScoper<PF_GPUDeviceSuite1,true> suite(in,kPFGPUDeviceSuite,kPFGPUDeviceSuiteVersion1,nullptr);
 if(!suite.get())return PF_Err_BAD_CALLBACK_PARAM;
 state->dispose(in,suite.get());return PF_Err_NONE;
}
inline PF_Err render(PF_InData* in,PF_SmartRenderInput* input,PF_EffectWorld* src,
                     PF_EffectWorld* dst,Params p,const std::shared_ptr<lutcache::Entry>& cache){
 if(!input||input->what_gpu!=PF_GPU_Framework_CUDA||!input->gpu_data||!dst)
  return PF_Err_BAD_CALLBACK_PARAM;
 auto& state=*const_cast<Device*>(static_cast<const Device*>(input->gpu_data));
 if(state.index!=input->device_index)return PF_Err_BAD_CALLBACK_PARAM;
 if(dst->width==0||dst->height==0)return PF_Err_NONE;
 AEFX_SuiteScoper<PF_GPUDeviceSuite1,true> suite(in,kPFGPUDeviceSuite,kPFGPUDeviceSuiteVersion1,nullptr);
 AEFX_SuiteScoper<PF_WorldSuite2,true> world(in,kPFWorldSuite,kPFWorldSuiteVersion2,nullptr);
 if(!suite.get()||!world.get())return PF_Err_BAD_CALLBACK_PARAM;
 std::lock_guard<std::mutex> lock(state.mutex);
 Context guard(state.d,state.info.contextPV);
 if(!guard.ok)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 auto get=[&](PF_EffectWorld* w,void** ptr)->bool{
  PF_PixelFormat fmt=PF_PixelFormat_INVALID;
  size_t bytes=0;
  if(w->width<=0||w->height<=0||w->rowbytes<int64_t(w->width)*16||
     w->rowbytes%16!=0)return false;
  if(world->PF_GetPixelFormat(w,&fmt)!=PF_Err_NONE||fmt!=PF_PixelFormat_GPU_BGRA128)
   return false;
  if(suite->GetGPUWorldData(in->effect_ref,w,ptr)!=PF_Err_NONE||!*ptr||
     (reinterpret_cast<uintptr_t>(*ptr)&15)!=0)return false;
  if(suite->GetGPUWorldSize(in->effect_ref,w,&bytes)!=PF_Err_NONE)return false;
  return bytes>=size_t(w->rowbytes)*size_t(w->height-1)+size_t(w->width)*16;
 };
 if(!get(dst,&p.dst))return PF_Err_INTERNAL_STRUCT_DAMAGED;
 p.dstW=dst->width;p.dstH=dst->height;p.dstPitch=dst->rowbytes;
 p.dstX=dst->origin_x;p.dstY=dst->origin_y;
 if(src&&src->width>0&&src->height>0){
  void* ptr=nullptr;if(!get(src,&ptr))return PF_Err_INTERNAL_STRUCT_DAMAGED;
  p.src=ptr;p.srcW=src->width;p.srcH=src->height;p.srcPitch=src->rowbytes;
  p.srcX=src->origin_x;p.srcY=src->origin_y;
 }
 if(!(p.flags&8)&&p.directCount==0){
  if(!cache)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  if(state.uploaded!=cache){
   cache->prepare();
   static_assert(sizeof(Color)==sizeof(Rgba));
   std::memcpy(state.pinned,cache->preparedRamp().data(),Device::rampBytes);
   if(state.d.upload(reinterpret_cast<uint64_t>(state.ramp),state.pinned,
                     Device::rampBytes,state.info.command_queuePV)!=0)
    return PF_Err_INTERNAL_STRUCT_DAMAGED;
   state.uploaded=cache;
  }
  p.ramp=static_cast<const Rgba*>(state.ramp);
 }
 const bool fast=p.color==0&&(p.alpha==0||p.alpha==5);
 void* args[]={&p};
 int err=state.d.launch(fast?state.fast:state.general,(p.dstW+31)/32,
                       (p.dstH+7)/8,1,32,8,1,0,state.info.command_queuePV,args,nullptr);
 // Only AE's own stream, no context/device-wide wait or image transfer.
 int completed=state.d.sync(state.info.command_queuePV);
 if(err||completed){state.uploaded.reset();return PF_Err_INTERNAL_STRUCT_DAMAGED;}
 return PF_Err_NONE;
}
}
