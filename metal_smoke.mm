#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include "gradation_metal_values.hpp"
#include "gradation_metal_blob.hpp"
#include "raster_core.hpp"
#include <iostream>
#include <vector>
#include <cmath>
using namespace gradation;
using namespace gradation::raster;
using namespace gradation::metal_render;
struct BGRA {float b,g,r,a;};
int main(){@autoreleasepool{
 id<MTLDevice> device=MTLCreateSystemDefaultDevice();
 if(!device){std::cout<<"SKIP: no Metal device\n";return 77;}
 NSError* error=nil;
 auto data=dispatch_data_create(kMetalLibrary,sizeof(kMetalLibrary),dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),DISPATCH_DATA_DESTRUCTOR_NONE);
 id<MTLLibrary> lib=[device newLibraryWithData:data error:&error];
 if(!lib){std::cerr<<"Metal library load failed\n";return 1;}
 auto queue=[device newCommandQueue];
 auto fast=[device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"gradation_fast"] error:&error];
 auto general=[device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"gradation_general"] error:&error];
 if(!queue||!fast||!general)return 2;
 constexpr int w=97,h=57;
 std::vector<Pixel32> input(w*h),expected(w*h);
 std::vector<BGRA> bgra(w*h);
 for(int i=0;i<w*h;i++){
  float a=(i%17)/16.f,r=a*(i%13)/12.f,g=a*(i%11)/10.f,b=a*(i%7)/6.f;
  input[i]={a,r,g,b};bgra[i]={b,g,r,a};
 }
 id<MTLBuffer> src=[device newBufferWithBytes:bgra.data() length:bgra.size()*sizeof(BGRA) options:MTLResourceStorageModeShared];
 id<MTLBuffer> dst=[device newBufferWithLength:bgra.size()*sizeof(BGRA) options:MTLResourceStorageModeShared];
 const Color lo{.17f,.31f,.73f,.24f},hi{.85f,.67f,.21f,.92f};
 const DirectRamp direct{lo,hi};
 Settings s{};s.startX=7.3f;s.startY=13.4f;s.endX=81.7f;s.endY=42.2f;
 Gradient grad(s,{{0,lo},{1,hi}});
 Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=grad.sample(i/4096.f);
 id<MTLBuffer> lut=[device newBufferWithBytes:ramp.data() length:sizeof(ramp) options:MTLResourceStorageModeShared];
 if(!src||!dst||!lut)return 3;
 constexpr blend::Config modes[]={
  {blend::Mode::Replace,blend::Mode::Replace,0},{blend::Mode::Replace,blend::Mode::Multiply,0},
  {blend::Mode::Normal,blend::Mode::Normal,0},{blend::Mode::Screen,blend::Mode::Multiply,0},
  {blend::Mode::SoftLight,blend::Mode::Multiply,0},{blend::Mode::Hue,blend::Mode::Normal,0}};
 double worst=0;int cases=0;
 for(auto shape:{Shape::Linear,Shape::Radial,Shape::Angle,Shape::Mapping})
 for(auto cfg:modes)for(bool useDirect:{false,true}){
  s.shape=shape;
  cuda_render::Params p{};
  p.srcW=p.dstW=w;p.srcH=p.dstH=h;p.srcPitch=p.dstPitch=w*16;
  p.startX=s.startX;p.startY=s.startY;p.dx=s.endX-s.startX;p.dy=s.endY-s.startY;
  p.invLen=1.f/std::max(std::hypot(p.dx,p.dy),1.e-6f);p.invScale=1.f;p.startAngle=fastAtan2(p.dy,p.dx);
  p.shape=int(shape);p.color=int(cfg.color);p.alpha=int(cfg.alpha);
  p.directCount=useDirect?2:0;p.first={lo.r,lo.g,lo.b,lo.a};p.last={hi.r,hi.g,hi.b,hi.a};
  const bool hasSource=shape==Shape::Mapping||cfg.color!=blend::Mode::Replace||cfg.alpha!=blend::Mode::Replace;
  auto v=marshal(p,hasSource);
  auto pipe=(p.color==0&&(p.alpha==0||p.alpha==5))?fast:general;
  auto cb=[queue commandBuffer];auto enc=[cb computeCommandEncoder];
  [enc setComputePipelineState:pipe];[enc setBuffer:src offset:0 atIndex:0];
  [enc setBuffer:dst offset:0 atIndex:1];[enc setBuffer:lut offset:0 atIndex:2];[enc setBytes:&v length:sizeof(v) atIndex:3];
  auto tw=pipe.threadExecutionWidth;
  [enc dispatchThreadgroups:MTLSizeMake((w+tw-1)/tw,h,1) threadsPerThreadgroup:MTLSizeMake(tw,1,1)];
  [enc endEncoding];[cb commit];[cb waitUntilCompleted];
  if(cb.status!=MTLCommandBufferStatusCompleted)return 4;
  Buffer in=hasSource?Buffer{reinterpret_cast<uint8_t*>(input.data()),w,h,w*16,0,0}:Buffer{};
  WritableBuffer out{reinterpret_cast<uint8_t*>(expected.data()),w,h,w*16,0,0};
  if(!render(in,out,gradation::raster::Depth::F32,s,ramp,nullptr,nullptr,cfg,useDirect?&direct:nullptr))return 5;
  auto* got=static_cast<const BGRA*>(dst.contents);
  for(int i=0;i<w*h;i++){
   const float a[]={got[i].a,got[i].r,got[i].g,got[i].b};
   const float b[]={expected[i].a,expected[i].r,expected[i].g,expected[i].b};
   for(int c=0;c<4;c++){
    double diff=std::abs(double(a[c])-b[c]);
    if(!std::isfinite(diff)||diff>5.e-4){std::cerr<<"Metal parity failure shape="<<p.shape<<" color="<<p.color<<" alpha="<<p.alpha<<" pixel="<<i<<" diff="<<diff<<"\n";return 6;}
    worst=std::max(worst,diff);
   }
  }
  ++cases;
 }
 std::cout<<"PASS Metal CPU comparison "<<cases<<" cases, max error "<<worst<<"\n";
 return 0;
}}
