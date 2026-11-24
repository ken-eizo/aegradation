// Isolated x64 JIT experiment: a leaf function with a fixed ARGB pixel
// encoded as an immediate. No AE hooks, injection, or writable executable
// pages. Windows ABI volatile registers only, aligned output only.
#include "../src/render/cpu/raster_parallel.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <memory>
using namespace gradation::raster;
struct Code {
 void* ptr=nullptr;
 ~Code(){if(ptr)VirtualFree(ptr,0,MEM_RELEASE);}
 using Fn=void(*)(void*,size_t);
 Fn build(uint32_t color){
  // mov eax,imm32; vmovd xmm0,eax; vpbroadcastd ymm0,xmm0;
  // loop: vmovntdq [rcx],ymm0; vmovntdq [rcx+32],ymm0;
  // add rcx,64; dec rdx; jnz loop; sfence; vzeroupper; ret.
  uint8_t code[]={0xb8,0,0,0,0,0xc5,0xf9,0x6e,0xc0,0xc4,0xe2,0x7d,0x58,0xc0,
   0xc5,0xfd,0xe7,0x01,0xc5,0xfd,0xe7,0x41,0x20,0x48,0x83,0xc1,0x40,
   0x48,0xff,0xca,0x75,0xee,0x0f,0xae,0xf8,0xc5,0xf8,0x77,0xc3};
  std::memcpy(code+1,&color,4);
  ptr=VirtualAlloc(nullptr,sizeof(code),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
  if(!ptr)return nullptr;
  std::memcpy(ptr,code,sizeof(code));
  DWORD old=0;
  if(!VirtualProtect(ptr,sizeof(code),PAGE_EXECUTE_READ,&old)||
     !FlushInstructionCache(GetCurrentProcess(),ptr,sizeof(code)))return nullptr;
  return reinterpret_cast<Fn>(ptr);
 }
};
__declspec(noinline) void staticFill(void* p,size_t n,uint32_t c){
 auto* dst=(__m256i*)p;const auto v=_mm256_set1_epi32(c);
 for(size_t i=0;i<n;i++){_mm256_stream_si256(dst+i*2,v);_mm256_stream_si256(dst+i*2+1,v);}
 _mm_sfence();
}
int main(){
 constexpr size_t bytes=1920ull*1080*4,blocks=bytes/64;
 auto* raw=(uint32_t*)VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!raw)return 1;
 struct Mem{void* p;~Mem(){VirtualFree(p,0,MEM_RELEASE);}} mem{raw};
 for(uint32_t color:{0x12345678u,0xffffffffu,0x00000000u}){
  Code c;auto fn=c.build(color);if(!fn){std::cout<<"JIT unavailable by platform policy\n";return 2;}
  fn(raw,blocks);
  for(size_t i=0;i<bytes/4;i++)if(raw[i]!=color)return 3;
  std::array<double,21> a{},b{};
  for(int i=0;i<21;i++){
   auto run=[&](bool jit){auto t=std::chrono::steady_clock::now();if(jit)fn(raw,blocks);else staticFill(raw,blocks,color);return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();};
   if(i%2){a[i]=run(false);b[i]=run(true);}else{b[i]=run(true);a[i]=run(false);}
  }
  std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
  std::cout<<"pixel "<<std::hex<<color<<std::dec<<" static "<<a[10]<<"ms JIT "<<b[10]<<"ms\n";
 }
 std::cout<<"PASS JIT exact output; W^X memory, no host integration\n";
}
