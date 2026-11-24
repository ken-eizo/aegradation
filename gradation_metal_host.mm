// Native AE Metal SmartFX backend. No Vulkan, CUDA, CPU image readback or private APIs.
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectSuites.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectPixelFormat.h"
#include "AEFX_SuiteHandlerTemplate.h"
#include "gradation_metal_host.hpp"
#include "gradation_metal_values.hpp"
#include "gradation_metal_blob.hpp"
#include "lut_cache.hpp"
#include <mutex>
#include <cstdlib>
namespace gradation::metal_render {
struct Device {
 A_u_long index=0;
 id<MTLDevice> device=nil;
 id<MTLCommandQueue> queue=nil;
 id<MTLComputePipelineState> fast=nil,general=nil;
 id<MTLBuffer> dummy=nil;
 std::mutex mutex;
 std::shared_ptr<lutcache::Entry> uploaded;
 id<MTLBuffer> ramp=nil;
};
PF_Err setup(PF_InData* in,PF_OutData* out,PF_GPUDeviceSetupExtra* ex){
 if(!in||!out||!ex||!ex->input||!ex->output)return PF_Err_BAD_CALLBACK_PARAM;
 ex->output->gpu_data=nullptr;
 out->out_flags2&=~PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
 if(!supports(ex->input->what_gpu)||!in->pica_basicP)return PF_Err_NONE;
 if(const char* v=std::getenv("GRADATION_DISABLE_METAL");v&&v[0]=='1')return PF_Err_NONE;
 @autoreleasepool {
  AEFX_SuiteScoper<PF_GPUDeviceSuite1,true> suite(in,kPFGPUDeviceSuite,kPFGPUDeviceSuiteVersion1,out);
  if(!suite.get())return PF_Err_NONE;
  auto state=std::make_unique<Device>();
  state->index=ex->input->device_index;
  PF_GPUDeviceInfo info{};
  if(suite->GetDeviceInfo(in->effect_ref,state->index,&info)!=PF_Err_NONE||
     !info.compatibleB||!supports(info.device_framework)||!info.devicePV||!info.command_queuePV)return PF_Err_NONE;
  state->device=(__bridge id<MTLDevice>)info.devicePV;
  state->queue=(__bridge id<MTLCommandQueue>)info.command_queuePV;
  if(state->queue.device!=state->device)return PF_Err_NONE;
  NSError* error=nil;
  dispatch_data_t data=dispatch_data_create(kMetalLibrary,sizeof(kMetalLibrary),dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),DISPATCH_DATA_DESTRUCTOR_NONE);
  id<MTLLibrary> lib=[state->device newLibraryWithData:data error:&error];
  if(!lib)return PF_Err_NONE;
  id<MTLFunction> f=[lib newFunctionWithName:@"gradation_fast"],g=[lib newFunctionWithName:@"gradation_general"];
  if(!f||!g)return PF_Err_NONE;
  state->fast=[state->device newComputePipelineStateWithFunction:f error:&error];
  state->general=[state->device newComputePipelineStateWithFunction:g error:&error];
  state->dummy=[state->device newBufferWithLength:16 options:MTLResourceStorageModeShared];
  if(!state->fast||!state->general||!state->dummy)return PF_Err_NONE;
  out->out_flags2|=PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
  ex->output->gpu_data=state.release();
  return PF_Err_NONE;
 }
}
PF_Err setdown(PF_InData* in,PF_GPUDeviceSetdownExtra* ex){
 if(!in||!ex||!ex->input)return PF_Err_BAD_CALLBACK_PARAM;
 // AE owns device and queue; ARC releases only our references.
 @autoreleasepool {delete static_cast<Device*>(ex->input->gpu_data);}
 return PF_Err_NONE;
}
PF_Err render(PF_InData* in,PF_SmartRenderInput* input,PF_EffectWorld* src,
 PF_EffectWorld* dst,cuda_render::Params p,const std::shared_ptr<lutcache::Entry>& cache){
 if(!in||!input||!supports(input->what_gpu)||!input->gpu_data||!dst)return PF_Err_BAD_CALLBACK_PARAM;
 auto& state=*const_cast<Device*>(static_cast<const Device*>(input->gpu_data));
 if(state.index!=input->device_index)return PF_Err_BAD_CALLBACK_PARAM;
 if(dst->width==0||dst->height==0)return PF_Err_NONE;
 @autoreleasepool {
  AEFX_SuiteScoper<PF_GPUDeviceSuite1,true> suite(in,kPFGPUDeviceSuite,kPFGPUDeviceSuiteVersion1,nullptr);
  AEFX_SuiteScoper<PF_WorldSuite2,true> world(in,kPFWorldSuite,kPFWorldSuiteVersion2,nullptr);
  if(!suite.get()||!world.get())return PF_Err_BAD_CALLBACK_PARAM;
  auto get=[&](PF_EffectWorld* w)->id<MTLBuffer>{
   if(w->width<=0||w->height<=0||w->rowbytes<int64_t(w->width)*16||w->rowbytes%16)return nil;
   PF_PixelFormat fmt=PF_PixelFormat_INVALID;
   if(world->PF_GetPixelFormat(w,&fmt)!=PF_Err_NONE||fmt!=PF_PixelFormat_GPU_BGRA128)return nil;
   void* raw=nullptr;size_t bytes=0;
   if(suite->GetGPUWorldData(in->effect_ref,w,&raw)!=PF_Err_NONE||!raw||
      suite->GetGPUWorldSize(in->effect_ref,w,&bytes)!=PF_Err_NONE)return nil;
   id<MTLBuffer> b=(__bridge id<MTLBuffer>)raw;
   const size_t need=size_t(w->rowbytes)*size_t(w->height-1)+size_t(w->width)*16;
   if(b.device!=state.device||bytes<need||b.length<need)return nil;
   return b;
  };
  id<MTLBuffer> dest=get(dst),source=state.dummy,ramp=state.dummy;
  if(!dest)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  p.dstW=dst->width;p.dstH=dst->height;p.dstPitch=dst->rowbytes;p.dstX=dst->origin_x;p.dstY=dst->origin_y;
  bool hasSource=src&&src->width>0&&src->height>0;
  if(hasSource){
   source=get(src);if(!source||source==dest)return PF_Err_INTERNAL_STRUCT_DAMAGED;
   p.srcW=src->width;p.srcH=src->height;p.srcPitch=src->rowbytes;p.srcX=src->origin_x;p.srcY=src->origin_y;
  }
  if(!(p.flags&8)&&p.directCount==0){
   if(!cache)return PF_Err_INTERNAL_STRUCT_DAMAGED;
   cache->prepare();
   // Only synchronize cache publication, not GPU execution. Every command
   // retains its immutable ramp buffer even if another MFR frame replaces it.
   std::lock_guard<std::mutex> lock(state.mutex);
   if(state.uploaded!=cache){
    static_assert(sizeof(Color)==sizeof(cuda_render::Rgba));
    id<MTLBuffer> next=[state.device newBufferWithBytes:cache->preparedRamp().data()
     length:4097*sizeof(Color) options:MTLResourceStorageModeShared];
    if(!next)return PF_Err_OUT_OF_MEMORY;
    state.ramp=next;state.uploaded=cache;
   }
   ramp=state.ramp;
  }
  MetalValues v=marshal(p,hasSource);
  id<MTLComputePipelineState> pipe=(p.color==0&&(p.alpha==0||p.alpha==5))?state.fast:state.general;
  id<MTLCommandBuffer> command=[state.queue commandBuffer];
  if(!command)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
  if(!encoder)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  [encoder setComputePipelineState:pipe];
  [encoder setBuffer:source offset:0 atIndex:0];
  [encoder setBuffer:dest offset:0 atIndex:1];
  [encoder setBuffer:ramp offset:0 atIndex:2];
  [encoder setBytes:&v length:sizeof(v) atIndex:3];
  NSUInteger tw=pipe.threadExecutionWidth;
  if(!tw||tw>pipe.maxTotalThreadsPerThreadgroup){[encoder endEncoding];return PF_Err_INTERNAL_STRUCT_DAMAGED;}
  NSUInteger th=std::max<NSUInteger>(1,std::min<NSUInteger>(8,pipe.maxTotalThreadsPerThreadgroup/tw));
  [encoder dispatchThreadgroups:MTLSizeMake((NSUInteger(p.dstW)+tw-1)/tw,(NSUInteger(p.dstH)+th-1)/th,1) threadsPerThreadgroup:MTLSizeMake(tw,th,1)];
  [encoder endEncoding];
  [command commit];
  // Wait only for this command buffer before SmartFX releases its worlds.
  [command waitUntilCompleted];
  return command.status==MTLCommandBufferStatusCompleted?PF_Err_NONE:PF_Err_INTERNAL_STRUCT_DAMAGED;
 }
}
}
