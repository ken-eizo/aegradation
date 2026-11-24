#include "gradient_core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
int main(){
 const std::array<Space,14> spaces={
  Space::SRGB,Space::LinearSRGB,Space::HSV,Space::HSL,Space::HSI,
  Space::Oklab,Space::Oklch,Space::CMYK,Space::YCbCr,Space::YCgCo,
  Space::CIECAM02,Space::HCT,Space::LMS,Space::Spectral};
 const char* names[]={"sRGB","LinearSRGB","HSV","HSL","HSI","Oklab",
  "Oklch","CMYK","YCbCr","YCgCo","CIECAM02","HCT","LMS","Spectral"};
 volatile float guard=0.f;
 for(size_t j=0;j<spaces.size();j++){
  std::vector<double> timing;
  uint64_t hash=14695981039346656037ull;
  for(int rep=0;rep<3;rep++){
   Settings settings{};settings.space=spaces[j];
   std::vector<Stop> stops={
     {0.f,{.22f,.83f,.42f,.3f}},
     {.19f,{.96f,.13f,.6f,.58f}},
     {.52f,{.1f,.32f,.9f,.79f}},
     {.78f,{.61f,.96f,.23f,.83f}},
     {1.f,{.96f,.74f,.2f,.91f}}
   };
   const auto begin=std::chrono::steady_clock::now();
   Gradient g(settings,std::move(stops));
   for(int i=0;i<=4096;i++){
    const Color c=g.sample((float)i/4096.f);
    // Full LUT hash: proves all four channels agree between both binaries.
    const float v[4]={c.r,c.g,c.b,c.a};
    for(float f:v){
     uint32_t bits{};std::memcpy(&bits,&f,sizeof(bits));
     hash^=bits;hash*=1099511628211ull;
    }
    guard+=c.a*0.00000001f;
   }
   const auto end=std::chrono::steady_clock::now();
   timing.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
  }
  std::sort(timing.begin(),timing.end());
  std::cout<<names[j]<<" median_4097_ms "<<std::fixed<<std::setprecision(4)
   <<timing[1]<<" FNV64 "<<std::hex<<hash<<std::dec<<"\n";
 }
 std::cout<<"guard "<<guard<<"\n";
}
