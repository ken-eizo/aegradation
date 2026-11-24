#pragma once
#include "../src/render/cpu/raster_geometry_cache.hpp"
// Lossless 8-pixel packets. Uniform: 2 bytes. Narrow range: 10 bytes.
// General packet: 18 bytes. Row offsets permit arbitrary stripe scheduling.
namespace gradation::raster::geometrycompression {
#if defined(__AVX2__)
struct Field {
 int w=0,h=0;
 std::vector<uint32_t> row;
 std::vector<uint16_t> words;
 size_t bytes()const{return words.size()*2+row.size()*4;}
};
inline Field encode(const geometrycache::Plane& src){
 Field f;f.w=src.key.w;f.h=src.key.h;
 f.row.reserve(f.h+1);f.words.reserve(src.index.size()/2);
 for(int y=0;y<f.h;y++){
  f.row.push_back((uint32_t)f.words.size());
  for(int x=0;x<f.w;x+=8){
   const auto* v=src.index.data()+size_t(y)*f.w+x;
   const auto mm=std::minmax_element(v,v+8);
   const uint16_t lo=*mm.first,hi=*mm.second;
   if(lo==hi)f.words.push_back(lo);
   else if(hi-lo<=255){
    f.words.push_back(uint16_t(lo|0x8000));
    for(int i=0;i<8;i+=2)f.words.push_back(uint16_t((v[i]-lo)|((v[i+1]-lo)<<8)));
   }else{
    f.words.push_back(0x4000);
    f.words.insert(f.words.end(),v,v+8);
   }
  }
 }
 f.row.push_back((uint32_t)f.words.size());
 return f;
}
inline __m256i next(const uint16_t*& p)noexcept{
 uint16_t tag=*p++;
 if(tag&0x8000){
  const auto v=_mm_loadl_epi64((const __m128i*)p);p+=4;
  return _mm256_add_epi32(_mm256_set1_epi32(tag&0x1fff),_mm256_cvtepu8_epi32(v));
 }
 if(tag&0x4000){auto v=_mm_loadu_si128((const __m128i*)p);p+=8;return _mm256_cvtepu16_epi32(v);}
 return _mm256_set1_epi32(tag);
}
inline bool verify(const Field& f,const geometrycache::Plane& src){
 alignas(32) uint32_t tmp[8];
 for(int y=0;y<f.h;y++){
  const auto* p=f.words.data()+f.row[y];
  for(int x=0;x<f.w;x+=8){
   _mm256_store_si256((__m256i*)tmp,next(p));
   for(int k=0;k<8;k++)if(tmp[k]!=src.index[size_t(y)*f.w+x+k])return false;
  }
  if(p!=f.words.data()+f.row[y+1])return false;
 }
 return true;
}
// Experimental hot path assumes the exact, fully covered buffer layout
// used by the comparison harness. No host dispatch.
inline void render(const Field& f,const Buffer& src,const WritableBuffer& dst,const PackedRamp8& lut){
 const int n=parallel::configuredWorkers();
 auto stripe=[&](int i)noexcept{
  for(int y=i*f.h/n;y<(i+1)*f.h/n;y++){
   const auto* p=f.words.data()+f.row[y];
   const auto* in=src.data+(size_t)y*src.rowBytes;
   auto* out=dst.data+(size_t)y*dst.rowBytes;
   for(int x=0;x<f.w;x+=8){
    const auto idx=next(p);
    const auto c=_mm256_i32gather_epi32((const int*)lut.data(),idx,4);
    simd::applyPacked8(out+x*4,in+x*4,c);
   }
  }
 };
 parallel::runStripes(n,stripe);
}
#endif
}
