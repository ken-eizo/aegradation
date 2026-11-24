"""Generate MSL from our canonical CUDA pixel math, with explicit Metal address spaces.
No CUDA toolkit is needed on macOS. Fail if source layout changes unexpectedly."""
from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[1]
params=(root/'src/render/gpu/shared/gradation_cuda_params.hpp').read_text()
fields=params.split(' int srcW,',1)[1].split('\n};',1)[0]
fields=' int srcW,'+fields
fields=fields.replace('std::uint32_t','uint')
src=(root/'src/render/gpu/cuda/gradation_cuda.cu').read_text()
src=src[src.index('DEV float sane'):src.index('template<bool Fast> DEV void pixel')]
src=src.replace('DEV ','inline ')
for f in ('fmin','fmax','fabs','sqrt','floor'):
 src=re.sub(r'\b'+f+r'f\(',f+'(',src)
src=src.replace('if(!p.src||','if(!p.hasSource||')
old='const auto* row=(const float4*)((const char*)p.src+(long long)sy*p.srcPitch);'
assert src.count(old)==1
src=src.replace(old,'device const float4* row=(device const float4*)(p.src+ulong(sy)*uint(p.srcPitch));')
header='''#include <metal_stdlib>
using namespace metal;
struct Rgba {float r,g,b,a;};
struct MetalValues {
 int hasSource;
'''+fields+'''\n};
struct Params {
 device const uchar* src;
 device uchar* dst;
 device const Rgba* ramp;
 int hasSource;
'''+fields+'''\n};
'''
# Copy individual fields; never marshal CPU pointers into a GPU constant buffer.
names=['hasSource']+re.findall(r'\b([A-Za-z_]\w*)\s*(?=,|;)',fields)
assert len(names)==29,names
copy='\n'.join(' p.'+n+'=v.'+n+';' for n in names)
tail='''
template<bool Fast> inline void pixel(Params p,uint2 gid) {
 int lx=int(gid.x),ly=int(gid.y);
 if(lx>=p.dstW||ly>=p.dstH)return;
 int x=lx+p.dstX,y=ly+p.dstY;
 Rgba src=source(p,x,y),out={0,0,0,0};
 if(!(p.flags&8)){
  Rgba g=sample(p,coordinate(p,x,y,src));
  if(Fast){float a=unit(g.a)*(p.alpha==0?1.f:unit(src.a));out={g.r*a,g.g*a,g.b*a,a};}
  else out=compose(p,src,g,x,y);
 }
 device float4* row=(device float4*)(p.dst+ulong(ly)*uint(p.dstPitch));
 row[lx]=float4(out.b,out.g,out.r,out.a);
}
'''
for name,fast in [('gradation_fast','true'),('gradation_general','false')]:
 tail+='''
kernel void '''+name+'''(device const uchar* src [[buffer(0)]],
 device uchar* dst [[buffer(1)]],device const Rgba* ramp [[buffer(2)]],
 constant MetalValues& v [[buffer(3)]],uint2 gid [[thread_position_in_grid]]) {
 Params p;
 p.src=src;p.dst=dst;p.ramp=ramp;
'''+copy+'\n pixel<'+fast+'>(p,gid);\n}\n'
Path(sys.argv[1]).write_text(header+src+tail)
# Generate host ABI directly from the same declarations; no pointer fields.
hostfields=fields.replace('uint frameSeed','std::uint32_t frameSeed')
Path(sys.argv[2]).write_text('#pragma once\n#include "gradation_cuda_params.hpp"\nnamespace gradation::metal_render {\nusing cuda_render::Rgba;\nstruct MetalValues { int hasSource;\n'+hostfields+'\n};\ninline MetalValues marshal(const cuda_render::Params& p,bool hasSource){MetalValues v{};\n v.hasSource=hasSource?1:0;\n'+ '\n'.join(' v.'+n+'=p.'+n+';' for n in names[1:])+'\n return v;}\nstatic_assert(sizeof(MetalValues)==152);\n}\n')

