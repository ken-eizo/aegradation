#include "../../src/render/cpu/raster_core.hpp"
using namespace gradation;
using namespace gradation::raster;
// This TU is intentionally compiled without /arch:AVX2 to serve as
// the independent scalar pixel rendering reference.
bool renderScalarReference(const Buffer& in,const WritableBuffer& out,Depth depth,
 const Settings& s,const Ramp& lut,const PackedRamp8* p8,const PackedRamp16* p16){
 return render(in,out,depth,s,lut,p8,p16);
}
