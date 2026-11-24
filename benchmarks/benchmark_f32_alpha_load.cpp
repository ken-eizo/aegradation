#include <immintrin.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
volatile float sink=0;
__forceinline __m256 baseline(const float* p){
 __m128 a0=_mm_loadu_ps(p+0),a1=_mm_loadu_ps(p+4),
         a2=_mm_loadu_ps(p+8),a3=_mm_loadu_ps(p+12);
 _MM_TRANSPOSE4_PS(a0,a1,a2,a3);
 __m128 b0=_mm_loadu_ps(p+16),b1=_mm_loadu_ps(p+20),
         b2=_mm_loadu_ps(p+24),b3=_mm_loadu_ps(p+28);
 _MM_TRANSPOSE4_PS(b0,b1,b2,b3);
 return _mm256_insertf128_ps(_mm256_castps128_ps256(a0),b0,1);
}
__forceinline __m256 custom(const float* p){
 const __m128 v0=_mm_loadu_ps(p),v1=_mm_loadu_ps(p+4);
 const __m128 v2=_mm_loadu_ps(p+8),v3=_mm_loadu_ps(p+12);
 const __m128 v4=_mm_loadu_ps(p+16),v5=_mm_loadu_ps(p+20);
 const __m128 v6=_mm_loadu_ps(p+24),v7=_mm_loadu_ps(p+28);
 const __m128 a=_mm_movelh_ps(_mm_unpacklo_ps(v0,v1),
                              _mm_unpacklo_ps(v2,v3));
 const __m128 b=_mm_movelh_ps(_mm_unpacklo_ps(v4,v5),
                              _mm_unpacklo_ps(v6,v7));
 return _mm256_insertf128_ps(_mm256_castps128_ps256(a),b,1);
}
__forceinline __m256 gather(const float* p){
 return _mm256_i32gather_ps(p,_mm256_setr_epi32(0,4,8,12,16,20,24,28),4);
}
int main(){
 constexpr size_t n=(size_t)1920*1080;
 std::vector<float> rgba(n*4),out(n);
 for(size_t i=0;i<n;i++){
  rgba[i*4]=(i%13==0)?.4f:1.f;
  rgba[i*4+1]=.5f;rgba[i*4+2]=.25f;rgba[i*4+3]=.75f;
 }
 auto run=[&](int method){
  for(size_t j=0;j<n;j+=8){
   const float* ptr=rgba.data()+j*4;
   __m256 x=method==0?baseline(ptr):method==1?custom(ptr):gather(ptr);
   _mm256_storeu_ps(out.data()+j,x);
  }
 };
 for(int m=0;m<3;m++){
  run(m);
  for(size_t i=0;i<n;i++)if(out[i]!=rgba[i*4]){
   std::cerr<<"bad alpha "<<m<<" at "<<i<<" got "<<out[i]<<"\n";return 2;}
  std::vector<double> timings;
  for(int i=0;i<13;i++){
   auto t0=std::chrono::steady_clock::now();
   run(m);
   auto t1=std::chrono::steady_clock::now();
   timings.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());
  }
  std::sort(timings.begin(),timings.end());
  sink+=out[(size_t)m*17];
  std::cout<<(m==0?"Baseline transposes":m==1?"SSE alpha-only":"AVX2 gather")
           <<" 1080p median_ms "<<std::fixed<<std::setprecision(3)
           <<timings[6]<<" min "<<timings[0]<<"\n";
 }
}
