#include "raster_core.hpp"
using namespace gradation;
using namespace gradation::raster;
// Compiled without /arch:AVX2 to exercise the independent scalar raster path.
bool scalarBlendReference(const Buffer& src,const WritableBuffer& dst,Depth depth,
 const Settings& s,const Ramp& lut,const PackedRamp8* p8,const PackedRamp16* p16,
 const blend::Config& config){
 return render(src,dst,depth,s,lut,p8,p16,config);
}
