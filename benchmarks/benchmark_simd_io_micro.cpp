#include "../src/render/cpu/raster_core.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
using namespace gradation::raster::simd;
volatile float checksum=0.f;
static inline Channels gather16Old(const uint8_t* ptr) noexcept {
 const __m256i index=_mm256_setr_epi32(0,2,4,6,8,10,12,14);
 const __m256i ar=_mm256_i32gather_epi32((const int*)ptr,index,4);
 const __m256i gb=_mm256_i32gather_epi32((const int*)(ptr+4),index,4);
 const __m256i mask=_mm256_set1_epi32(65535);
 const __m256 n=_mm256_set1_ps(1.f/32768.f);
 return {
  _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_and_si256(ar,mask)),n),
  _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_srli_epi32(ar,16)),n),
  _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_and_si256(gb,mask)),n),
  _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_srli_epi32(gb,16)),n)
 };
}
static inline void store32Old(uint8_t* dst,__m256 a,__m256 r,__m256 g,__m256 b)noexcept {
 __m128 a0=_mm256_castps256_ps128(a),r0=_mm256_castps256_ps128(r);
 __m128 g0=_mm256_castps256_ps128(g),b0=_mm256_castps256_ps128(b);
 _MM_TRANSPOSE4_PS(a0,r0,g0,b0);
 _mm_storeu_ps((float*)(dst+0),a0);
 _mm_storeu_ps((float*)(dst+16),r0);
 _mm_storeu_ps((float*)(dst+32),g0);
 _mm_storeu_ps((float*)(dst+48),b0);
 __m128 a1=_mm256_extractf128_ps(a,1),r1=_mm256_extractf128_ps(r,1);
 __m128 g1=_mm256_extractf128_ps(g,1),b1=_mm256_extractf128_ps(b,1);
 _MM_TRANSPOSE4_PS(a1,r1,g1,b1);
 _mm_storeu_ps((float*)(dst+64),a1);
 _mm_storeu_ps((float*)(dst+80),r1);
 _mm_storeu_ps((float*)(dst+96),g1);
 _mm_storeu_ps((float*)(dst+112),b1);
}
int main(){
 constexpr int N=1920*1080;
 constexpr int NN=(N/8)*8;
 std::vector<uint8_t> source((size_t)NN*8);
 for(int i=0;i<NN;i++){
  Pixel16 p{(uint16_t)((i*23)%32769),
            (uint16_t)((i*11)%32769),
            (uint16_t)((i*7)%32769),
            (uint16_t)((i*37)%32769)};
  std::memcpy(source.data()+(size_t)i*8,&p,8);
 }
 std::vector<float> af(NN),rf(NN),gf(NN),bf(NN);
 std::vector<float> ao(NN),ro(NN),go(NN),bo(NN);
 auto decode=[&](bool newLoad){
  for(int i=0;i<NN;i+=8){
   const uint8_t* p=source.data()+(size_t)i*8;
   const Channels c=newLoad?source16(p):gather16Old(p);
   _mm256_storeu_ps(af.data()+i,c.a);
   _mm256_storeu_ps(rf.data()+i,c.r);
   _mm256_storeu_ps(gf.data()+i,c.g);
   _mm256_storeu_ps(bf.data()+i,c.b);
  }
 };
 decode(true);
 ao=af;ro=rf;go=gf;bo=bf;
 decode(false);
 if(af!=ao||rf!=ro||gf!=go||bf!=bo){
  std::cerr<<"16bit contiguous read differs from gather\n";return 2;
 }
 std::vector<uint8_t> image((size_t)NN*16),reference(image.size());
 auto store=[&](bool newStore){
  for(int i=0;i<NN;i+=8){
   const __m256 a=_mm256_loadu_ps(af.data()+i);
   const __m256 r=_mm256_loadu_ps(rf.data()+i);
   const __m256 g=_mm256_loadu_ps(gf.data()+i);
   const __m256 b=_mm256_loadu_ps(bf.data()+i);
   if(newStore)storeFloatARGB8(image.data()+(size_t)i*16,a,r,g,b);
   else store32Old(image.data()+(size_t)i*16,a,r,g,b);
  }
 };
 store(false);reference=image;store(true);
 if(image!=reference){std::cerr<<"F32 store is not bit identical\n";return 3;}
 auto measure=[&](auto fn){
  std::vector<double> samples;
  fn();fn();
  for(int i=0;i<15;i++){
   const auto t0=std::chrono::steady_clock::now();
   fn();
   const auto t1=std::chrono::steady_clock::now();
   samples.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());
  }
  std::sort(samples.begin(),samples.end());
  return samples[7];
 };
 // Interleave operations so CPU power state and caches are comparable.
 const double oldDecode=measure([&]{decode(false);});
 const double newDecode=measure([&]{decode(true);});
 const double oldStore=measure([&]{store(false);});
 const double newStore=measure([&]{store(true);});
 checksum=af[17]+gf[191]+(float)image[64];
 std::cout<<std::setprecision(4)<<std::fixed;
 std::cout<<"16bpc 1080p RGBA decode: GATHER "<<oldDecode
  <<" ms, SEQUENTIAL "<<newDecode<<" ms, speedup "<<oldDecode/newDecode<<"x\n";
 std::cout<<"32bpc 1080p RGBA store: SSE "<<oldStore
  <<" ms, AVX2 "<<newStore<<" ms, speedup "<<oldStore/newStore<<"x\n";
 std::cout<<"Both outputs byte-exact, checksum "<<checksum<<"\n";
}
