#pragma once
#include <cuda.h>
#include "AE_EffectPixelFormat.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectGPUSuites.h"
namespace mockgpu {
CUcontext context=nullptr;
CUstream stream=nullptr;
int allocations=0;
PF_Err info(PF_ProgPtr,A_u_long index,PF_GPUDeviceInfo* out){
 if(index||!out)return PF_Err_BAD_CALLBACK_PARAM;
 *out={};out->device_framework=PF_GPU_Framework_CUDA;out->compatibleB=TRUE;
 out->contextPV=context;out->command_queuePV=stream;return PF_Err_NONE;
}
PF_Err alloc(PF_ProgPtr,A_u_long,size_t bytes,void** out){
 CUdeviceptr p=0;if(cuMemAlloc(&p,bytes)!=CUDA_SUCCESS)return PF_Err_OUT_OF_MEMORY;
 *out=(void*)p;++allocations;return PF_Err_NONE;
}
PF_Err free(PF_ProgPtr,A_u_long,void* p){
 if(cuMemFree((CUdeviceptr)p)!=CUDA_SUCCESS)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 --allocations;return PF_Err_NONE;
}
PF_Err hostAlloc(PF_ProgPtr,A_u_long,size_t bytes,void** out){
 if(cuMemAllocHost(out,bytes)!=CUDA_SUCCESS)return PF_Err_OUT_OF_MEMORY;
 ++allocations;return PF_Err_NONE;
}
PF_Err hostFree(PF_ProgPtr,A_u_long,void* p){
 if(cuMemFreeHost(p)!=CUDA_SUCCESS)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 --allocations;return PF_Err_NONE;
}
PF_Err data(PF_ProgPtr,PF_EffectWorld* w,void** out){*out=w->data;return PF_Err_NONE;}
PF_Err bytes(PF_ProgPtr,PF_EffectWorld* w,size_t* out){*out=size_t(w->rowbytes)*w->height;return PF_Err_NONE;}
PF_Err format(const PF_EffectWorld*,PF_PixelFormat* out){*out=PF_PixelFormat_GPU_BGRA128;return PF_Err_NONE;}
PF_GPUDeviceSuite1 gpu{};
PF_WorldSuite2 worlds{};
SPErr acquire(const char* name,int32 version,const void** out){
 *out=nullptr;
 if(std::strcmp(name,kPFGPUDeviceSuite)==0&&version==kPFGPUDeviceSuiteVersion1)*out=&gpu;
 if(std::strcmp(name,kPFWorldSuite)==0&&version==kPFWorldSuiteVersion2)*out=&worlds;
 return *out?kSPNoError:kSPBadParameterError;
}
SPErr release(const char*,int32){return kSPNoError;}
void initSuites(SPBasicSuite& basic){
 gpu.GetDeviceInfo=info;gpu.AllocateDeviceMemory=alloc;gpu.FreeDeviceMemory=free;
 gpu.AllocateHostMemory=hostAlloc;gpu.FreeHostMemory=hostFree;
 gpu.GetGPUWorldData=data;gpu.GetGPUWorldSize=bytes;worlds.PF_GetPixelFormat=format;
 basic.AcquireSuite=acquire;basic.ReleaseSuite=release;
}
}
