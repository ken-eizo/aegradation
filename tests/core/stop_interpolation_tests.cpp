#include "../../src/core/stop_interpolation.hpp"
#include "../../src/core/gradient_core.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
using namespace gradation;
struct StopData {float pos=0,r=0,g=0,b=0,a=1;uint32_t id=0;};
using Result=std::array<StopData,32>;
static bool finite(const StopData& s){
 return std::isfinite(s.pos)&&std::isfinite(s.r)&&std::isfinite(s.g)&&
        std::isfinite(s.b)&&std::isfinite(s.a);
}
static Gradient gradient(const StopData* stops,int count,Space space){
 std::vector<Stop> vector;
 for(int k=0;k<count;k++){
  const auto& s=stops[k];
  vector.push_back({s.pos,{s.r,s.g,s.b,s.a}});
 }
 Settings settings{};settings.space=space;
 return Gradient(settings,std::move(vector));
}
static float colorError(Color a,Color b){
 return std::max({std::abs(a.r-b.r),std::abs(a.g-b.g),
                  std::abs(a.b-b.b),std::abs(a.a-b.a)});
}
static bool checkMorph(const StopData* left,int nl,const StopData* right,int nr,
                       bool testVisual=true){
 Result out{};
 const int n0=stopmorph::interpolate(left,nl,right,nr,0.f,out.data());
 if(n0!=nl||std::memcmp(out.data(),left,nl*sizeof(StopData))!=0){
  std::cerr<<"Left endpoint identity failed\n";return false;
 }
 const int n1=stopmorph::interpolate(left,nl,right,nr,1.f,out.data());
 if(n1!=nr||std::memcmp(out.data(),right,nr*sizeof(StopData))!=0){
  std::cerr<<"Right endpoint identity failed\n";return false;
 }
 float last=0;
 for(int k=0;k<=100;k++){
  float u=k/100.f;
  const int n=stopmorph::interpolate(left,nl,right,nr,u,out.data());
  if(n!=(u<=0?nl:u>=1?nr:std::max(nl,nr))){
   std::cerr<<"Count failed "<<n<<" at "<<u<<"\n";return false;
  }
  for(int i=0;i<n;i++){
   if(!finite(out[i])||out[i].pos<last-1e-5f||
      out[i].pos<0||out[i].pos>1||out[i].a<0||out[i].a>1){
    std::cerr<<"Corrupt stop i="<<i<<" u="<<u<<"\n";return false;
   }
   last=out[i].pos;
  }
  last=0;
 }
 if(!testVisual)return true;
 for(Space space:{Space::SRGB,Space::HSV,Space::Oklab,
                 Space::LinearSRGB,Space::Spectral}){
  const Gradient gl=gradient(left,nl,space);
  const Gradient gr=gradient(right,nr,space);
  for(float u:{.00001f,.001f,.49f,.50f,.51f,.999f,.99999f}){
   const int n=stopmorph::interpolate(left,nl,right,nr,u,out.data());
   const Gradient gm=gradient(out.data(),n,space);
   const Gradient& endpoint=u<.5f?gl:gr;
   float worst=0;
   // No discontinuity at either keyframe when the topology changes.
   if(u<.01f||u>.99f){
    for(int i=0;i<=300;i++){
     const float t=i/300.f;
     const Color one=endpoint.sample(t),two=gm.sample(t);
     worst=std::max(worst,colorError(one,two));
    }
    const float tolerance=(space==Space::Spectral?.18f:.08f);
    if(worst>tolerance){
     std::cerr<<"Endpoint continuity failed space "<<(int)space
      <<" u "<<u<<" peak channel error "<<worst<<"\n";
     return false;
    }
   }
  }
 }
 return true;
}
int main(){
 const StopData two[]={
  {0,.10f,.18f,.20f,1.f,1},
  {1,.91f,.79f,.72f,1.f,2}};
 const StopData three[]={
  {0,.10f,.18f,.20f,1.f,1},
  {.45f,.98f,.21f,.05f,.62f,3},
  {1,.91f,.79f,.72f,1.f,2}};
 if(!checkMorph(two,2,three,3)||
    !checkMorph(three,3,two,2))return 1;
 // Different colors, positions and transparency on both keyframes.
 const StopData four[]={
  {0,.24f,.2f,.10f,.3f,1},
  {.15f,.9f,.1f,.85f,.2f,4},
  {.67f,.1f,.76f,.95f,.8f,6},
  {1,.70f,.85f,.10f,1.f,2}};
 const StopData six[]={
  {0,.24f,.2f,.10f,.3f,1},
  {.18f,.95f,.75f,.3f,.95f,8},
  {.40f,.36f,.25f,.90f,.52f,4},
  {.73f,.1f,.76f,.95f,.8f,6},
  {.91f,.7f,.05f,.5f,.1f,9},
  {1,.70f,.85f,.10f,1.f,2}};
 if(!checkMorph(four,4,six,6)||
    !checkMorph(six,6,four,4))return 2;
 // Maximum legal capacity: must never allocate more than the 32 slots
 // AE's serialized arbitrary-data payload has always contained.
 std::array<StopData,32> thirtyTwo{};
 for(int i=0;i<32;i++){
  thirtyTwo[(size_t)i]={(float)i/31,
       (float)((i*7)%13)/12,(float)((i*11)%17)/16,
       (float)((i*3)%19)/18,(float)(i%5+1)/5,(uint32_t)(i+100)};
 }
 std::array<StopData,31> thirtyOne{};
 std::copy_n(thirtyTwo.begin(),31,thirtyOne.begin());
 if(!checkMorph(thirtyTwo.data(),32,two,2,false)||
    !checkMorph(two,2,thirtyTwo.data(),32,false)||
    !checkMorph(thirtyTwo.data(),32,thirtyOne.data(),31,false)||
    !checkMorph(thirtyOne.data(),31,thirtyTwo.data(),32,false))return 3;
 // Stress identity fallback and zero-distance anchors (duplicate positions).
 const StopData repeated[]={
  {0,0,0,0,1,1},{.25f,.8f,.2f,.1f,.5f,4},
  {.25f,.8f,.2f,.1f,.5f,5},{1,1,1,1,1,2}};
 if(!checkMorph(two,2,repeated,4,false)||
    !checkMorph(repeated,4,two,2,false))return 4;
 // Rendered-gradient temporal continuity, not just stop values.
 // A one-frame jump near t=0.5 used to reproduce the original bug.
 double worstFrameDelta=0;
 for(Space space:{Space::SRGB,Space::HSV,Space::Oklab,Space::Spectral}){
  for(int direction=0;direction<2;direction++){
   const StopData* begin=direction?three:two;
   const StopData* end=direction?two:three;
   const int bn=direction?3:2,en=direction?2:3;
   Result prev{},next{};
   int nPrev=stopmorph::interpolate(begin,bn,end,en,0.f,prev.data());
   Gradient previous=gradient(prev.data(),nPrev,space);
   for(int frame=1;frame<=200;frame++){
    const float u=frame/200.f;
    const int n=stopmorph::interpolate(begin,bn,end,en,u,next.data());
    const Gradient current=gradient(next.data(),n,space);
    for(int i=0;i<=128;i++){
     const float t=i/128.f;
     worstFrameDelta=std::max(worstFrameDelta,
       (double)colorError(current.sample(t),previous.sample(t)));
    }
    prev=next;
    previous=current;
   }
  }
 }
 if(worstFrameDelta>.14){
  std::cerr<<"Temporal discontinuity: max per-frame RGBA delta "<<worstFrameDelta<<"\n";
  return 6;
 }
 std::atomic<int> problems{0};
 std::array<std::thread,12> threads;
 for(int t=0;t<12;t++)threads[(size_t)t]=std::thread([&,t]{
  Result result{};
  for(int f=0;f<1000;f++){
   const float u=(float)f/999.f;
   const bool reverse=(t&1)!=0;
   const int n=reverse?
      stopmorph::interpolate(thirtyTwo.data(),32,two,2,u,result.data()):
      stopmorph::interpolate(two,2,thirtyTwo.data(),32,u,result.data());
   if(n<2||n>32||(!reverse&&f>0&&f<999&&n!=32))
    problems.fetch_add(1,std::memory_order_relaxed);
  }
 });
 for(auto& t:threads)t.join();
 if(problems.load())return 5;
 // Interpolation-only cost (no LUT build, rendering or AE API).
 Result result{};
 volatile double checksum=0;
 const auto begin=std::chrono::steady_clock::now();
 for(int f=0;f<10000;f++){
  const int n=stopmorph::interpolate(thirtyTwo.data(),32,two,2,
                         (f%997+1)/998.f,result.data());
  checksum+=result[(size_t)(f%n)].r;
 }
 const auto end=std::chrono::steady_clock::now();
 const double micros=std::chrono::duration<double,std::micro>(end-begin).count()/10000.;
 const auto beginDense=std::chrono::steady_clock::now();
 for(int f=0;f<5000;f++){
  const int n=stopmorph::interpolate(thirtyTwo.data(),32,thirtyOne.data(),31,
                         (f%997+1)/998.f,result.data());
  checksum+=result[(size_t)(f%n)].a;
 }
 const auto endDense=std::chrono::steady_clock::now();
 const double denseMicros=std::chrono::duration<double,std::micro>(
                                     endDense-beginDense).count()/5000.;
 std::cout<<"PASS: changing stop counts 2/3/4/6/32, endpoint continuity "
    <<"across sRGB/HSV/Oklab/linear/spectral, 12-thread stress; "
    <<"32->2 average "<<micros<<" us, 32->31 average "
    <<denseMicros<<" us, checksum "<<checksum<<"\n";
 return 0;
}
