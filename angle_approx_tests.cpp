#include "raster_core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
using namespace gradation;
using namespace gradation::raster;
using namespace gradation::raster::simd;
int main(){
 constexpr float PI=3.14159265358979323846f;
 alignas(32)float xv[8],yv[8],angles[8];
 double maxAbs=0;
 for(int q=0;q<6000;q++){
  for(int i=0;i<8;i++){
   const float t=(float)((q*8+i)%47999)/47999.f*2.f*PI-PI;
   const float r=(float)((q*8+i)%79+1)/17.f;
   xv[i]=std::cos(t)*r;
   yv[i]=std::sin(t)*r;
  }
  const __m256 out=vatan2(_mm256_load_ps(yv),_mm256_load_ps(xv));
  _mm256_store_ps(angles,out);
  for(int i=0;i<8;i++){
   float target=std::atan2(yv[i],xv[i]);
   float delta=angles[i]-target;
   // pi/-pi differ by 2pi but describe the same periodic angle.
   delta-=std::round(delta/(2.f*PI))*(2.f*PI);
   maxAbs=std::max(maxAbs,std::abs((double)delta));
   if(!std::isfinite(delta)||std::abs(delta)>8e-6f){
    std::cerr<<"AVX2 atan2 error="<<delta<<" angle "<<target<<"\n";
    return 1;
   }
  }
 }
 std::cout<<"PASS: 48,000 AVX2 atan2 vectors, max angular error "
          <<maxAbs<<" radians\n";
}
