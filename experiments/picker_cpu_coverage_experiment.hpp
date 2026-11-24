#pragma once
// Experimental Vulkan-independent coverage backend. Uses the same authored
// Slughorn atlas curves as the Vulkan renderer, not an approximated GDI icon.
// Axis-aligned masks only. Projection, text layout, paint/clip composition
// and presentation are deliberately not advertised as implemented here.
#include <slugia/vector_atlas.hpp>
#include <slughorn/render.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace gradation::picker::cpu_coverage {
struct Mask {
 int left=0,top=0,width=0,height=0;
 std::vector<float> values;
};
inline Mask rasterize(const slughorn::render::Sampler& sampler,
 const slugia::Rect& dst,bool banded=true) {
 if(!(dst.width>0&&dst.height>0)||!std::isfinite(dst.x)||
    !std::isfinite(dst.y)||!std::isfinite(dst.width)||!std::isfinite(dst.height)||
    std::abs(dst.x)>1.e6f||std::abs(dst.y)>1.e6f||
    dst.width>1024||dst.height>1024)
  throw std::invalid_argument("Invalid software coverage bounds");
 Mask out;
 out.left=int(std::floor(dst.x-.5f));out.top=int(std::floor(dst.y-.5f));
 out.width=int(std::ceil(dst.x+dst.width+.5f))-out.left;
 out.height=int(std::ceil(dst.y+dst.height+.5f))-out.top;
 out.values.resize(size_t(out.width)*out.height);
 const auto& shape=sampler.shape;
 if(shape.width<=0||shape.height<=0)return out;
 const float dx=float(shape.width)/dst.width,dy=float(shape.height)/dst.height;
 const float ppeX=1.f/dx,ppeY=1.f/dy;
 for(int y=0;y<out.height;++y){
  const float ey=float(shape.bearingY)-(float(out.top+y)+.5f-dst.y)*dy;
  for(int x=0;x<out.width;++x){
   const float ex=float(shape.bearingX)+(float(out.left+x)+.5f-dst.x)*dx;
   const auto coverage=banded?sampler.renderSampleBanded(ex,ey,ppeX,ppeY):
                              sampler.renderSample(ex,ey,ppeX,ppeY);
   out.values[size_t(y)*out.width+x]=std::clamp(float(coverage.fill),0.f,1.f);
  }
 }
 return out;
}
} // namespace gradation::picker::cpu_coverage
