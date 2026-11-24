#include "../../src/render/cpu/raster_parallel.hpp"
#include <chrono>
#include <iostream>
using namespace gradation;
using namespace gradation::raster;
template<class P> bool test(Depth depth){
 constexpr int w=1920,h=1080;
 std::vector<P> in((size_t)w*h),ref(in.size()),out(in.size());
 for(size_t i=0;i<in.size();i++){
  if constexpr(sizeof(P)==4)in[i]={(uint8_t)(i%13?255:i%256),0,0,0};
  else in[i]={(uint16_t)(i%13?32768:i%32769),0,0,0};
 }
 Settings s{};s.startX=235.4f;s.endX=1668.8f;s.startY=s.endY=540;
 const auto stride=w*(int)sizeof(P);
 for(int mode=0;mode<3;mode++){
  s.loop=mode==1;s.reflect=mode==2;s.invert=mode==2;s.offsetPx=mode*13.3f;
  Gradient g(s,{{0,{.2f,.4f,.8f,.35f}},{.41f,{.91f,.32f,.22f,.7f}},{1,{.1f,.7f,.3f,.98f}}});
  Ramp ramp{};for(int i=0;i<=4096;i++)ramp[i]=g.sample(i/4096.f);
  const auto p8=makePacked8(ramp);const auto p16=makePacked16(ramp);
  blend::Config cfg{blend::Mode::Replace,blend::Mode::Multiply,0};
  auto run=[&](int cached){
   if(cached==2)return gradation::raster::parallel::render({(const uint8_t*)in.data(),w,h,stride,7,11},{(uint8_t*)out.data(),w,h,stride,7,11},depth,s,ramp,&p8,&p16,cfg);
   if(cached)return render({(const uint8_t*)in.data(),w,h,stride,7,11},
    {(uint8_t*)out.data(),w,h,stride,7,11},depth,s,ramp,&p8,&p16,cfg);
   for(int y=0;y<h;y++)if(!render({(const uint8_t*)in.data()+size_t(y)*stride,w,1,stride,7,11+y},
    {(uint8_t*)ref.data()+size_t(y)*stride,w,1,stride,7,11+y},depth,s,ramp,&p8,&p16,cfg))return false;
   return true;
  };
  if(!run(0)||!run(1)||std::memcmp(ref.data(),out.data(),in.size()*sizeof(P))||!run(2)||std::memcmp(ref.data(),out.data(),in.size()*sizeof(P)))return false;
  if(mode==0){
   for(int cached:{0,1,2}){
    std::array<double,11> ms{};
    for(auto& t:ms){auto a=std::chrono::steady_clock::now();if(!run(cached))return false;
     t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();}
    std::sort(ms.begin(),ms.end());std::cout<<sizeof(P)*2<<"bpc "<<(cached==2?"parallel":cached?"cached":"reference")<<" "<<ms[5]<<"ms\n";
   }
  }
 }
 return true;
}
// Check every possible unsigned input alpha for representative colors,
// including the signed boundary, against exact integer arithmetic.
bool q15(){
#if defined(__AVX2__)
 alignas(32) uint16_t color[16],alpha[16],out[16];
 for(unsigned c:{0u,1u,2u,16383u,16384u,32766u,32767u,32768u,32769u,65535u}){
  for(unsigned a=0;a<65536;a+=16){
   for(int i=0;i<16;i++){color[i]=(uint16_t)c;alpha[i]=(uint16_t)(a+i);}
   const auto cv=_mm256_load_si256((const __m256i*)color);
   const auto av=_mm256_load_si256((const __m256i*)alpha);
   _mm256_store_si256((__m256i*)out,simd::mulQ15Unsigned(cv,av));
   for(int i=0;i<16;i++)
    if(out[i]!=(uint16_t)(((uint64_t)c*(a+i)+16384)>>15))return false;
   if(c<32768){
    // Four pixels with individual source alpha, replicated by VPSHUFB.
    _mm256_store_si256((__m256i*)out,simd::mulSourceAlpha16Low(cv,av));
    for(int i=0;i<16;i++)
     if(out[i]!=(uint16_t)(((uint64_t)c*(a+(i/4)*4)+16384)>>15))return false;
   }
  }
 }
#endif
 return true;
}
int main(){if(!q15())return 4;if(!test<Pixel8>(Depth::U8)||!test<Pixel16>(Depth::U16))return 2;
 std::cout<<"PASS integer exact horizontal rows 8/16bpc, loop/reflect/invert, partial alpha\n";}
