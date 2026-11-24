#include "gradation_metal_values.hpp"
#include <cstddef>
#include <type_traits>
#include <iostream>
using gradation::metal_render::MetalValues;
static_assert(std::is_standard_layout_v<MetalValues>);
static_assert(sizeof(MetalValues)==152 && alignof(MetalValues)==4);
static_assert(offsetof(MetalValues,srcW)==4);
static_assert(offsetof(MetalValues,dstW)==24);
static_assert(offsetof(MetalValues,startX)==44);
static_assert(offsetof(MetalValues,shape)==76);
static_assert(offsetof(MetalValues,frameSeed)==96);
static_assert(offsetof(MetalValues,first)==104);
int main(){
 gradation::cuda_render::Params p{};
 p.srcW=1920;p.dstPitch=32768;p.startX=27.5f;p.flags=7;p.frameSeed=0x12345678;p.first={.1f,.2f,.3f,.4f};
 auto v=gradation::metal_render::marshal(p,true);
 if(v.hasSource!=1||v.srcW!=1920||v.dstPitch!=32768||v.startX!=27.5f||v.flags!=7||v.frameSeed!=0x12345678||v.first.a!=.4f)return 1;
 std::cout<<"PASS pointer-free Metal ABI and shared parameter marshaling\n";
}
