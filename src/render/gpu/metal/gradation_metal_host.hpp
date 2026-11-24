#pragma once
#include "AE_EffectGPUSuites.h"
#include "../shared/gradation_cuda_params.hpp"
#include <memory>
namespace gradation::lutcache { struct Entry; }
namespace gradation::metal_render {
inline bool supports(PF_GPU_Framework f) noexcept {return f==PF_GPU_Framework_METAL;}
PF_Err setup(PF_InData*,PF_OutData*,PF_GPUDeviceSetupExtra*);
PF_Err setdown(PF_InData*,PF_GPUDeviceSetdownExtra*);
PF_Err render(PF_InData*,PF_SmartRenderInput*,PF_EffectWorld*,PF_EffectWorld*,
              cuda_render::Params,const std::shared_ptr<lutcache::Entry>&);
}
