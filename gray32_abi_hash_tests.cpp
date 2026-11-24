#include "raster_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace gradation;
using namespace gradation::raster;
int main(){
 uint64_t hash=14695981039346656037ull;
 int scenarios=0;
 auto mix=[&](const std::vector<uint8_t>& image){
  for(uint8_t c:image){hash^=c;hash*=1099511628211ull;}
 };
 for(int stopCount:{2,3})for(int alphaType=0;alphaType<3;alphaType++)
 for(int grayscale=0;grayscale<2;grayscale++)
 for(int signedZeros=0;signedZeros<2;signedZeros++)
 for(int shape=0;shape<4;shape++)
 for(int trans=0;trans<4;trans++)for(int crop=0;crop<2;crop++){
  constexpr int W=257,H=25;
  Settings s{};s.shape=(Shape)shape;
  s.startX=20.5f;s.startY=10.25f;
  s.endX=200.75f;s.endY=trans%2?61.5f:10.25f;
  if(trans==1)s.reflect=true;
  if(trans==2)s.invert=true;
  if(trans==3){s.loop=true;s.offsetPx=13.25f;}
  const float a0=alphaType==0?1.f:alphaType==1?.25f:0.f;
  const float a1=alphaType==2?0.f:.75f;
  const Color first{signedZeros?-0.f:.2f,
      signedZeros?0.f:.2f,signedZeros?0.f:(grayscale?.2f:.7f),a0};
  const Color mid{signedZeros?0.f:.7f,
      signedZeros?-0.f:(grayscale?.7f:.35f),
      signedZeros?0.f:(grayscale?.7f:.1f),alphaType==0?1.f:.45f};
  const Color last{signedZeros?0.f:.94f,
      signedZeros?0.f:(grayscale?.94f:.18f),
      signedZeros?-0.f:(grayscale?.94f:.49f),a1};
  const DirectRamp direct{first,last,mid,.43f,stopCount};
  const std::vector<Stop> stops=stopCount==2?
    std::vector<Stop>{{0,first},{1,last}}:
    std::vector<Stop>{{0,first},{.43f,mid},{1,last}};
  Gradient grad(s,stops);
  Ramp ramp{};
  for(int i=0;i<=4096;i++)ramp[(size_t)i]=grad.sample(i/4096.f);
  const int bw=W+8, bh=H+7, rowbytes=bw*16;
  std::vector<uint8_t> pixels((size_t)rowbytes*bh),out((size_t)rowbytes*H,0xa5);
  for(int y=0;y<bh;y++)for(int x=0;x<bw;x++){
   const float al=((x+y)%9==0)?0.f:((x*5+y)%11==0)?.5f:1.f;
   const Pixel32 px{al,al*.27f,al*.67f,al*.91f};
   std::memcpy(pixels.data()+(size_t)y*rowbytes+x*16,&px,16);
  }
  const Buffer src{pixels.data(),bw,bh,rowbytes,crop?4:0,crop?3:0};
  WritableBuffer dst{out.data(),W,H,rowbytes,0,0};
  const blend::Config cfg{blend::Mode::Replace,blend::Mode::Multiply,0};
  if(!render(src,dst,Depth::F32,s,ramp,nullptr,nullptr,cfg,&direct))return 2;
  mix(out);
  scenarios++;
 }
 std::cout<<"CASES "<<scenarios<<" HASH 0x"<<std::hex<<hash<<std::dec<<"\n";
}
