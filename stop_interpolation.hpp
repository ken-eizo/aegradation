#pragma once
// Stop-count-changing AE arbitrary-keyframe interpolation.
// Pure, allocation-free and AE-host-independent; no global state.
// Interpolates stop positions/RGBA while giving inserted/deleted stops
// zero visual influence at the opposite keyframe by collapsing them
// onto a paired existing stop with IDENTICAL color and alpha.
// This preserves exact keyframe gradients in ALL interpolation spaces,
// without a cross-space RGB sampler or extra effect parameters.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gradation::stopmorph {
constexpr int Capacity=32;

// Source type needs: pos,r,g,b,a (floats) and id (uint32).
template<class Stop>
inline float matchingCost(const Stop& a,const Stop& b) noexcept {
 const float color=std::abs(a.r-b.r)+std::abs(a.g-b.g)+
                   std::abs(a.b-b.b)+std::abs(a.a-b.a);
 const float distance=std::abs(a.pos-b.pos);
 // Preserve stable stop IDs whenever possible; insertions/deletions
 // should not scramble the surviving stops' animated motion.
 return (a.id!=0&&a.id==b.id)?(-100.f+distance):distance*4.f+color*.15f;
}

// Match every stop of the SMALLER ordered list to a subsequence of
// the larger list, with first/last stops forced to match first/last.
// Dynamic programming avoids visually surprising pairings after edits.
template<class Stop>
inline void matchedSubsequence(const Stop* small,int ns,
                               const Stop* large,int nl,
                               int* smallToLarge) noexcept {
 for(int i=0;i<ns;i++)smallToLarge[i]=-1;
 smallToLarge[0]=0;
 smallToLarge[ns-1]=nl-1;
 if(ns<=2)return;
 constexpr float inf=1.e20f;
 std::array<std::array<float,Capacity+1>,Capacity+1> dp{};
 std::array<std::array<uint8_t,Capacity+1>,Capacity+1> take{};
 for(int i=1;i<=Capacity;i++)dp[(size_t)i][0]=inf;
 // Interior of both lists: endpoints already paired.
 const int n=ns-2,m=nl-2;
 for(int i=1;i<=n;i++){
  for(int j=1;j<=m;j++){
   const float skip=dp[(size_t)i][(size_t)j-1];
   const float match=dp[(size_t)i-1][(size_t)j-1]+
      matchingCost(small[i],large[j]);
   if(match<=skip){
    dp[(size_t)i][(size_t)j]=match;
    take[(size_t)i][(size_t)j]=1;
   }else dp[(size_t)i][(size_t)j]=skip;
  }
 }
 int i=n,j=m;
 while(i>0&&j>0){
  if(take[(size_t)i][(size_t)j]){
   smallToLarge[i]=j;
   --i;--j;
  }else --j;
 }
}

// Return all stops of the larger keyframe, including temporary ghost stops.
// At u=0 and u=1 callers preserve original exact keyframe data.
// For interior u, count is max(left.count,right.count), never >32.
// Both ordered endpoint ramps remain exactly representable at u->0/1.
template<class Stop>
inline int interpolate(const Stop* left,int nl,const Stop* right,int nr,
                       float progress,Stop* result) noexcept {
 if(!left||!right||!result||
    nl<2||nr<2||nl>Capacity||nr>Capacity)return 0;
 const float u=std::isfinite(progress)?
               std::clamp(progress,0.f,1.f):0.f;
 if(u<=0.f){
  for(int i=0;i<nl;i++)result[i]=left[i];
  return nl;
 }
 if(u>=1.f){
  for(int i=0;i<nr;i++)result[i]=right[i];
  return nr;
 }
 if(nl==nr){
  for(int i=0;i<nl;i++){
   result[i]=left[i];
   result[i].pos=left[i].pos+(right[i].pos-left[i].pos)*u;
   result[i].r=left[i].r+(right[i].r-left[i].r)*u;
   result[i].g=left[i].g+(right[i].g-left[i].g)*u;
   result[i].b=left[i].b+(right[i].b-left[i].b)*u;
   result[i].a=left[i].a+(right[i].a-left[i].a)*u;
   result[i].id=(uint32_t)(i+1);
  }
  return nl;
 }
 // For a count-changing morph, pair all stops of the smaller keyframe
 // with selected stops of the larger, and create "ghost" endpoints for
 // insertions/deletions. Ghost = exact duplicate of an existing stop.
 const bool rightLarger=nr>nl;
 const Stop* small=rightLarger?left:right;
 const Stop* large=rightLarger?right:left;
 const int ns=std::min(nl,nr),nLarge=std::max(nl,nr);
 std::array<int,Capacity> paired{};
 matchedSubsequence(small,ns,large,nLarge,paired.data());
 std::array<int,Capacity> largeToSmall{};
 largeToSmall.fill(-1);
 for(int i=0;i<ns;i++)largeToSmall[(size_t)paired[i]]=i;
 for(int j=0;j<nLarge;j++){
  int i=largeToSmall[(size_t)j];
  if(i<0){
   int nearest=0;
   float distance=std::numeric_limits<float>::infinity();
   for(int k=0;k<ns;k++){
    const float d=std::abs(large[j].pos-large[paired[k]].pos);
    if(d<distance){distance=d;nearest=k;}
   }
   i=nearest;
  }
  const Stop& a=rightLarger?small[i]:large[j];
  const Stop& b=rightLarger?large[j]:small[i];
  Stop& output=result[j];
  output=a;
  output.pos=a.pos+(b.pos-a.pos)*u;
  output.r=a.r+(b.r-a.r)*u;
  output.g=a.g+(b.g-a.g)*u;
  output.b=a.b+(b.b-a.b)*u;
  output.a=a.a+(b.a-a.a)*u;
  // IDs at intermediate times are for the UI only and never enter the
  // pixel cache key. Stable-by-order IDs avoid ghost ID collisions.
  output.id=(uint32_t)(j+1);
 }
 return nLarge;
}
} // namespace gradation::stopmorph
