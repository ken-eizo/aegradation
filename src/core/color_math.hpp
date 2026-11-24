// Internal color calculations. Included inside namespace gradation after Color and Space.
namespace detail {
inline float signedPow(float x,float p){return std::copysign(std::pow(std::abs(x),p),x);}
struct XYZ {float x,y,z;};
struct Tri {float x,y,z;};
inline Tri mul(const float m[9],Tri p){
 return {m[0]*p.x+m[1]*p.y+m[2]*p.z,m[3]*p.x+m[4]*p.y+m[5]*p.z,m[6]*p.x+m[7]*p.y+m[8]*p.z};
}
inline constexpr float RGBXYZ[9]={.4124564f,.3575761f,.1804375f,.2126729f,.7151522f,.0721750f,.0193339f,.1191920f,.9503041f};
inline constexpr float XYZRGB[9]={3.2404542f,-1.5371385f,-.4985314f,-.9692660f,1.8760108f,.0415560f,.0556434f,-.2040259f,1.0572252f};
inline constexpr float CAT02[9]={.7328f,.4296f,-.1624f,-.7036f,1.6975f,.0061f,.0030f,.0136f,.9834f};
inline constexpr float INV_CAT02[9]={1.096124f,-.278869f,.182745f,.454369f,.473533f,.072098f,-.009628f,-.005698f,1.015326f};
inline constexpr float HPE[9]={.38971f,.68898f,-.07868f,-.22981f,1.18340f,.04641f,0,0,1};
inline constexpr float INV_HPE[9]={1.910197f,-1.112124f,.201908f,.370950f,.629054f,0,0,0,1};
inline constexpr float CAM16[9]={.401288f,.650173f,-.051461f,-.250268f,1.204414f,.045854f,-.002079f,.048952f,.953127f};
inline constexpr float INV_CAM16[9]={1.86206786f,-1.01125463f,.14918677f,.38752654f,.62144744f,-.00897398f,-.01584150f,-.03412294f,1.04996444f};
inline constexpr Tri D65{95.047f,100.f,108.883f};
inline XYZ xyzFromRgb(Color c){
 auto v=mul(RGBXYZ,{srgbDecode(c.r),srgbDecode(c.g),srgbDecode(c.b)});
 return {100.f*v.x,100.f*v.y,100.f*v.z};
}
inline Color rgbFromXyz(XYZ c,float a){
 auto v=mul(XYZRGB,{c.x*.01f,c.y*.01f,c.z*.01f});
 return {srgbEncode(v.x),srgbEncode(v.y),srgbEncode(v.z),a};
}
inline Color rgbToHSI(Color c){
 float sum=c.r+c.g+c.b;
 float sat=(sum>1e-10f)?1.f-3.f*std::min({c.r,c.g,c.b})/sum:0.f;
 float a=c.r-c.g,b=c.r-c.b,d=c.g-c.b;
 float den=std::sqrt(a*a+b*d);
 float angle=0.f;
 if(den>1e-8f){
  angle=std::acos(std::clamp((a+b)*.5f/den,-1.f,1.f));
  if(c.b>c.g)angle=6.28318530718f-angle;
 }
 return {angle/6.28318530718f,clamp(sat),sum/3.f,c.a};
}
inline Color hsiToRgb(Color c){
 constexpr float TAU=6.28318530718f;
 float h=(c.r-std::floor(c.r))*TAU;
 float s=clamp(c.g),i=c.b;
 float v1=0,v2=0,v3=0;
 if(h<2.094395102f){
  v3=i*(1-s);v1=i*(1+s*std::cos(h)/std::max(1e-6f,std::cos(1.04719755f-h)));v2=3*i-v1-v3;
 }else if(h<4.188790205f){
  h-=2.094395102f;v1=i*(1-s);v2=i*(1+s*std::cos(h)/std::max(1e-6f,std::cos(1.04719755f-h)));v3=3*i-v1-v2;
 }else{
  h-=4.188790205f;v2=i*(1-s);v3=i*(1+s*std::cos(h)/std::max(1e-6f,std::cos(1.04719755f-h)));v1=3*i-v2-v3;
 }
 return {v1,v2,v3,c.a};
}
inline Color rgbToLMS(Color c){
 auto xyz=xyzFromRgb(c);auto v=mul(HPE,{xyz.x,xyz.y,xyz.z});
 return {v.x*.01f,v.y*.01f,v.z*.01f,c.a};
}
inline Color lmsToRgb(Color c){
 auto v=mul(INV_HPE,{100*c.r,100*c.g,100*c.b});
 return rgbFromXyz({v.x,v.y,v.z},c.a);
}
struct Appearance {
 float white[3],adapt[3],aw,fl,n,nbb,z,cz; bool cam16;
};
inline float resp(float x,float fl,bool cam16){
 float u=std::pow(fl*std::abs(x)/100.f,.42f);
 return std::copysign(400.f*u/(u+27.13f),x)+(cam16?0.f:.1f);
}
inline float unresp(float v,float fl,bool cam16){
 float p=v-(cam16?0.f:.1f);
 float a=std::min(399.999f,std::abs(p));
 return std::copysign((100.f/fl)*std::pow(27.13f*a/(400.f-a),1.f/.42f),p);
}
inline Appearance init(bool cam16){
 Appearance st{};st.cam16=cam16;
 constexpr float la=64.f,yb=20.f,yw=100.f,F=1.f,c=.69f,Nc=1.f;
 float k=1.f/(5.f*la+1.f),k4=k*k*k*k;
 st.fl=.2f*k4*5.f*la+.1f*(1.f-k4)*(1.f-k4)*std::cbrt(5.f*la);
 st.n=yb/yw;st.nbb=.725f/std::pow(st.n,.2f);
 st.z=1.48f+std::sqrt(st.n);st.cz=c*st.z;
 float D=std::clamp(F*(1.f-std::exp(-(la+42.f)/92.f)/3.6f),0.f,1.f);
 auto white=mul(cam16?CAM16:CAT02,D65);
 st.white[0]=white.x;st.white[1]=white.y;st.white[2]=white.z;
 for(int j=0;j<3;j++)st.adapt[j]=D*100.f/st.white[j]+1.f-D;
 Tri adapted={white.x*st.adapt[0],white.y*st.adapt[1],white.z*st.adapt[2]};
 if(!cam16){
  auto xyz=mul(INV_CAT02,adapted);adapted=mul(HPE,xyz);
 }
 float r=resp(adapted.x,st.fl,cam16),g=resp(adapted.y,st.fl,cam16),b=resp(adapted.z,st.fl,cam16);
 st.aw=(2*r+g+b/20.f-(cam16?0.f:.305f))*st.nbb;
 return st;
}
inline Color toAppearance(Color c,bool cam16){
 static const Appearance st02=init(false),st16=init(true);
 const auto& st=cam16?st16:st02;
 auto xyz=xyzFromRgb(c);
 auto v=mul(cam16?CAM16:CAT02,{xyz.x,xyz.y,xyz.z});
 v.x*=st.adapt[0];v.y*=st.adapt[1];v.z*=st.adapt[2];
 if(!cam16){v=mul(HPE,mul(INV_CAT02,v));}
 float r=resp(v.x,st.fl,cam16),g=resp(v.y,st.fl,cam16),b=resp(v.z,st.fl,cam16);
 float aa=r-12.f*g/11.f+b/11.f,bb=(r+g-2.f*b)/9.f;
 float h=std::atan2(bb,aa);if(h<0)h+=6.28318530718f;
 float A=(2.f*r+g+b/20.f-(cam16?0.f:.305f))*st.nbb;
 float J=100.f*std::pow(std::max(0.f,A/st.aw),st.cz);
 float et=(std::cos(h+2.f)+3.8f)*.25f;
 float t= (50000.f/13.f)*st.nbb*et*std::hypot(aa,bb)/
          std::max(1e-6f,r+g+21.f*b/20.f);
 float C=std::pow(std::max(0.f,t),.9f)*std::sqrt(std::max(0.f,J/100.f))*
         std::pow(1.64f-std::pow(.29f,st.n),.73f);
 return {J/100.f,C/100.f,h/6.28318530718f,c.a};
}
inline XYZ appearanceXYZ(Color c,bool cam16){
 static const Appearance st02=init(false),st16=init(true);
 const auto& st=cam16?st16:st02;
 float J=std::max(0.f,c.r*100.f),C=std::max(0.f,c.g*100.f),h=c.b*6.28318530718f;
 if(J<1e-6f)return {0,0,0};
 float A=st.aw*std::pow(J/100.f,1.f/st.cz);
 float p2=A/st.nbb+(cam16?0.f:.305f);
 float alpha=C/std::sqrt(J/100.f);
 float t=std::pow(std::max(0.f,alpha)/std::pow(1.64f-std::pow(.29f,st.n),.73f),1.f/.9f);
 float et=(std::cos(h+2.f)+3.8f)*.25f;
 float K=(50000.f/13.f)*st.nbb*et;
 float hc=std::cos(h),hs=std::sin(h);
 float base=(2.f+1.05f)*460.f*p2/1403.f;
 float a=0,b=0;
 if(t>1e-8f){
  float chroma=t*base/(K-t*(-671.f*hc-6588.f*hs)/1403.f);
  a=chroma*hc;b=chroma*hs;
 }
 Tri v={(460*p2+451*a+288*b)/1403.f,(460*p2-891*a-261*b)/1403.f,(460*p2-220*a-6300*b)/1403.f};
 v.x=unresp(v.x,st.fl,cam16);v.y=unresp(v.y,st.fl,cam16);v.z=unresp(v.z,st.fl,cam16);
 if(!cam16){v=mul(CAT02,mul(INV_HPE,v));}
 v.x/=st.adapt[0];v.y/=st.adapt[1];v.z/=st.adapt[2];
 v=mul(cam16?INV_CAM16:INV_CAT02,v);
 return {v.x,v.y,v.z};
}
inline Color fromAppearance(Color c,bool cam16){return rgbFromXyz(appearanceXYZ(c,cam16),c.a);}
inline float lstar(float Y){
 float y=std::max(0.f,Y)/100.f;
 return y<=.008856451679f?903.2962963f*y:116.f*std::cbrt(y)-16.f;
}
inline float yFromLstar(float L){
 return L<=8.f?100.f*L/903.2962963f:100.f*std::pow((L+16.f)/116.f,3.f);
}
inline Color rgbToHCT(Color c){
 auto v=toAppearance(c,true);v.r=lstar(xyzFromRgb(c).y)/100.f;
 return {v.b,v.g,v.r,c.a}; // normalized hue, CAM16 chroma, CIELAB tone
}
inline Color hctToRgb(Color c){
 float targetY=yFromLstar(c.b*100.f);
 if(targetY<1e-7f)return {0,0,0,c.a};
 // Solve J for target Y. CAM16 inverse is monotone for ordinary display-gamut colors.
 float lo=0.f,hi=1.f,j=.5f;
 for(int n=0;n<11;n++){
  j=(lo+hi)*.5f;
  auto x=appearanceXYZ({j,c.g,c.r,c.a},true);
  if(x.y<targetY)lo=j;else hi=j;
 }
 auto xyz=appearanceXYZ({(lo+hi)*.5f,c.g,c.r,c.a},true);
 return rgbFromXyz(xyz,c.a);
}
#include "spectral_38.hpp"
}
