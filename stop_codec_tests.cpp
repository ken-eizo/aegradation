#include "stop_value_codec.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <string>
using namespace gradation::stopui;
static int failure=0;
void verify(bool pass,const char* desc){
 if(!pass){std::cerr<<"FAIL "<<desc<<"\n";failure++;}
}
int main(){
 constexpr std::array<Depth,3> depth={Depth::Bpc8,Depth::Bpc16,Depth::Bpc32};
 const float sample[]={0.f,1.f,.5f,.25f,.00001f,.994f,0.123456791f,0.987654328f};
 for(const Depth d:depth){
  const float unit=d==Depth::Bpc32?1.e-8f:1.f/channelMax(d);
  for(const float x:sample){
   const auto str=formatChannel(x,d);
   float y=-1.f;
   verify(parseChannel(str.c_str(),d,y),"format parses");
   if(d==Depth::Bpc32){
    verify(std::abs(x-y)<=.000051f,"32bpc display rounded to at most four decimals");
   }else{
    verify(std::abs(x-y)<=unit*.51f,"integer output quantized with half LSB max");
   }
  }
  for(const wchar_t* bad:{L"",L"-1",L"inf",L"nan",L"1.0.4",L"0junk",L"  "})
    {float y=123.f;verify(!parseChannel(bad,d,y),"malformed numeric rejected");}
  float tmp=0;
  verify(!parseChannel(d==Depth::Bpc8?L"256":d==Depth::Bpc16?L"32769":L"1.0000002",d,tmp),"overrange rejected");
  verify(parseChannel(d==Depth::Bpc8?L"255":d==Depth::Bpc16?L"32768":L"1.0",d,tmp)&&tmp==1.f,"white/opaque maps to 1");
  verify(parseChannel(d==Depth::Bpc8?L"128":d==Depth::Bpc16?L"16384":L"0.5",d,tmp),"midpoint accepted");
  if(d==Depth::Bpc16)verify(tmp==.5f,"16bpc 16384 exact midpoint");
  if(d==Depth::Bpc8)verify(std::abs(tmp-128.f/255.f)<1.e-8f,"8bpc code value");
  const std::array<float,4> original{.123456791f,.234567895f,.345678926f,.456789047f};
  std::array<bool,4> changed{false,false,false,false};
  std::array<std::wstring,4> fields{};
  for(int k=0;k<4;k++)fields[k]=formatChannel(original[k],d);
  std::array<float,4> output{};
  verify(parseRGBA(fields,d,changed,original,output),"unchanged fields parsed");
  for(int k=0;k<4;k++)verify(std::memcmp(&original[k],&output[k],4)==0,"unchanged float preserved");
  changed[1]=true;
  fields[1]=d==Depth::Bpc8?L"201":d==Depth::Bpc16?L"32767":L"0.333333333";
  verify(parseRGBA(fields,d,changed,original,output),"single edited channel parsed");
  for(int k:{0,2,3})verify(std::memcmp(&original[k],&output[k],4)==0,"untouched channel not requantized");
 }
 std::cout<<"Stop codec tests 8/16/32bpc: "<<failure<<" failures\n";
 return failure?1:0;
}
