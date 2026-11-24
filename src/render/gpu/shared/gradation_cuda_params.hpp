#pragma once
#include <cstdint>
namespace gradation::cuda_render {
struct Rgba { float r,g,b,a; };
// POD shared by the host and offline-compiled CUDA module. AE GPU
// worlds are premultiplied BGRA float32. No image allocations here.
struct Params {
 const void* src;
 void* dst;
 const Rgba* ramp;
 int srcW,srcH,srcPitch,srcX,srcY;
 int dstW,dstH,dstPitch,dstX,dstY;
 float startX,startY,dx,dy,invLen,invScale,offset,startAngle;
 int shape,color,alpha,flags,directCount;
 std::uint32_t frameSeed;
 float middlePosition;
 Rgba first,middle,last;
};
static_assert(sizeof(Rgba)==16);
}
