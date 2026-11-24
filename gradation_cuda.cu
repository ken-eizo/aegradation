#include "gradation_cuda_params.hpp"
#include <cuda_runtime.h>
using namespace gradation::cuda_render;
#define DEV __device__ __forceinline__
DEV float sane(float x){return isfinite(x)?x:0.f;}
DEV float unit(float x){return fminf(1.f,fmaxf(0.f,sane(x)));}
DEV float finiteRange(float x){return fabsf(x)<1.e8f?x:0.f;}
DEV float lerp1(float a,float b,float t){return a+(b-a)*t;}
DEV Rgba lerp4(Rgba a,Rgba b,float t){
 return {lerp1(a.r,b.r,t),lerp1(a.g,b.g,t),lerp1(a.b,b.b,t),lerp1(a.a,b.a,t)};
}
DEV Rgba sample(Params p,float t){
 if(p.directCount==2)return lerp4(p.first,p.last,t);
 if(p.directCount==3){
  if(t<p.middlePosition)return lerp4(p.first,p.middle,t/p.middlePosition);
  return lerp4(p.middle,p.last,(t-p.middlePosition)/(1.f-p.middlePosition));
 }
 const float u=t*4096.f;
 const int i=min(4095,(int)u);
 return lerp4(p.ramp[i],p.ramp[i+1],u-i);
}
DEV float atanFast(float z){
 const float zz=z*z;
 return z*(.9999956488609314f+zz*(-.3329949676990509f+
  zz*(.1956379562616348f+zz*(-.1212436482310295f+
  zz*(.05748184770345688f+zz*(-.013482107780873775f))))));
}
DEV float atan2Fast(float y,float x){
 float ax=fabsf(x),ay=fabsf(y);
 if(ax+ay<1.e-20f)return 0.f;
 float a=ax>=ay?atanFast(ay/fmaxf(ax,1.e-20f)):
                 1.57079632679489661923f-atanFast(ax/fmaxf(ay,1.e-20f));
 if(x<0)a=3.14159265358979323846f-a;
 if(y<0)a=-a;
 return a;
}
DEV float coordinate(Params p,int x,int y,Rgba s){
 const float xx=float(x)-p.startX,yy=float(y)-p.startY;
 float t=0.f;
 switch(p.shape){
 case 0:t=(xx*p.dx+yy*p.dy)*(p.invLen*p.invLen);break;
 case 1:t=sqrtf(xx*xx+yy*yy)*p.invLen;break;
 case 2:t=(atan2Fast(yy,xx)-p.startAngle)*(1.f/6.28318530717958647692f);
        t-=floorf(t);break;
 default:
  if(s.a>1.e-10f&&isfinite(s.a)&&isfinite(s.r)&&isfinite(s.g)&&isfinite(s.b))
   t=unit((.2126f*s.r+.7152f*s.g+.0722f*s.b)/s.a);
  break;
 }
 t=finiteRange(finiteRange(t)*p.invScale+p.offset);
 if(p.flags&2){t=t-2.f*floorf(t*.5f);t=1.f-fabsf(1.f-t);}
 else if(p.flags&1)t-=floorf(t);
 else t=unit(t);
 return p.flags&4?1.f-t:t;
}
DEV float random01(int x,int y,unsigned seed){
 unsigned v=(unsigned)x*0x9e3779b9u^(unsigned)y*0x85ebca6bu^seed*0xc2b2ae35u;
 v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;
 return (v>>8)*(1.f/16777216.f);
}
DEV float burn(float b,float s){return s<=0.f?0.f:1.f-(1.f-b)/s;}
DEV float dodge(float b,float s){return s>=1.f?1.f:b/(1.f-s);}
DEV float vivid(float b,float s){return s<=.5f?burn(b,2.f*s):dodge(b,2.f*(s-.5f));}
DEV float channel(float b,float s,int m){
 switch(m){
 case 0:case 1:case 2:case 3:return s;
 case 4:case 9:return fminf(b,s);
 case 5:return b*s;
 case 6:return burn(b,s);
 case 7:return 1.f-fminf(1.f,(1.f-b)/fmaxf(s,1.e-8f));
 case 8:return b+s-1.f;
 case 10:case 15:return b+s;
 case 11:case 16:return fmaxf(b,s);
 case 12:return b+s-b*s;
 case 13:return dodge(b,s);
 case 14:return fminf(1.f,b/fmaxf(1.f-s,1.e-8f));
 case 17:return b<=.5f?2.f*b*s:1.f-2.f*(1.f-b)*(1.f-s);
 case 18:{
  if(s<=.5f)return b-(1.f-2.f*s)*b*(1.f-b);
  const float d=b<=.25f?((16.f*b-12.f)*b+4.f)*b:sqrtf(fmaxf(0.f,b));
  return b+(2.f*s-1.f)*(d-b);
 }
 case 19:return s<=.5f?2.f*b*s:1.f-2.f*(1.f-b)*(1.f-s);
 case 20:return b+2.f*s-1.f;
 case 21:return vivid(b,s);
 case 22:return s<=.5f?fminf(b,2.f*s):fmaxf(b,2.f*s-1.f);
 case 23:return vivid(b,s)<.5f?0.f:1.f;
 case 24:case 25:return fabsf(b-s);
 case 26:return b+s-2.f*b*s;
 case 27:return b-s;
 case 28:return fabsf(s)<1.e-8f?0.f:b/s;
 default:return s;
 }
}
DEV float lum(Rgba c){return .3f*c.r+.59f*c.g+.11f*c.b;}
DEV float mn(Rgba c){return fminf(c.r,fminf(c.g,c.b));}
DEV float mx(Rgba c){return fmaxf(c.r,fmaxf(c.g,c.b));}
DEV float sat(Rgba c){return mx(c)-mn(c);}
DEV Rgba setLum(Rgba c,float target){
 float d=target-lum(c);
 c.r+=d;c.g+=d;c.b+=d;
 float l=lum(c),n=mn(c),x=mx(c);
 if(n<0.f){
  float t=l!=n?l/(l-n):0.f;
  c.r=l+(c.r-l)*t;c.g=l+(c.g-l)*t;c.b=l+(c.b-l)*t;
 }
 if(x>1.f){
  float t=x!=l?(1.f-l)/(x-l):0.f;
  c.r=l+(c.r-l)*t;c.g=l+(c.g-l)*t;c.b=l+(c.b-l)*t;
 }
 return c;
}
DEV Rgba setSat(Rgba c,float target){
 float n=mn(c),x=mx(c);
 if(x<=n)return {0,0,0,c.a};
 float d=target/(x-n);
 return {(c.r-n)*d,(c.g-n)*d,(c.b-n)*d,c.a};
}
DEV Rgba colors(Rgba b,Rgba g,int m){
 switch(m){
 case 9:return lum(b)<=lum(g)?b:g;
 case 16:return lum(b)>=lum(g)?b:g;
 case 29:return setLum(setSat(g,sat(b)),lum(b));
 case 30:return setLum(setSat(b,sat(g)),lum(b));
 case 31:return setLum(g,lum(b));
 case 32:return setLum(b,lum(g));
 default:return {channel(b.r,g.r,m),channel(b.g,g.g,m),channel(b.b,g.b,m),1};
 }
}
DEV Rgba source(Params p,int x,int y){
 int sx=x-p.srcX,sy=y-p.srcY;
 if(!p.src||sx<0||sy<0||sx>=p.srcW||sy>=p.srcH)return {0,0,0,0};
 const auto* row=(const float4*)((const char*)p.src+(long long)sy*p.srcPitch);
 float4 z=row[sx];
 return {z.z,z.y,z.x,z.w};
}
DEV Rgba compose(Params p,Rgba s,Rgba g,int x,int y){
 float as=unit(s.a),ag=unit(g.a),ao;
 switch(p.alpha){
 case 0:ao=ag;break;
 case 1:ao=ag+as*(1.f-ag);break;
 case 2:ao=random01(x,y,0)<ag?1.f:as;break;
 case 3:ao=random01(x,y,p.frameSeed+1)<ag?1.f:as;break;
 default:ao=unit(channel(as,ag,p.alpha));break;
 }
 if(p.color==0)return {sane(g.r)*ao,sane(g.g)*ao,sane(g.b)*ao,ao};
 if(p.color==1){
  float na=ag+as*(1.f-ag);
  if(na<=1.e-10f)return {0,0,0,ao};
  float f=ao/na,w=1.f-ag;
  return {sane(((as>1.e-10f?sane(s.r):0.f)*w+sane(g.r)*ag)*f),
          sane(((as>1.e-10f?sane(s.g):0.f)*w+sane(g.g)*ag)*f),
          sane(((as>1.e-10f?sane(s.b):0.f)*w+sane(g.b)*ag)*f),ao};
 }
 float inv=as>1.e-10f?1.f/as:0.f;
 Rgba b{as>0.f?sane(s.r)*inv:0.f,as>0.f?sane(s.g)*inv:0.f,
        as>0.f?sane(s.b)*inv:0.f,as};
 Rgba fg{sane(g.r),sane(g.g),sane(g.b),ag};
 float cov=ag;
 if(p.color==2)cov=random01(x,y,0)<ag?1.f:0.f;
 if(p.color==3)cov=random01(x,y,p.frameSeed+1)<ag?1.f:0.f;
 const Rgba c=colors(b,fg,p.color);
 const float na=cov+as*(1.f-cov);
 if(na<=1.e-10f)return {0,0,0,ao};
 float sw=as*(1.f-cov),fw=cov*(1.f-as),ow=cov*as;
 return {sane((b.r*sw+fg.r*fw+c.r*ow)/na)*ao,
         sane((b.g*sw+fg.g*fw+c.g*ow)/na)*ao,
         sane((b.b*sw+fg.b*fw+c.b*ow)/na)*ao,ao};
}
template<bool Fast> DEV void pixel(Params p){
 int lx=blockIdx.x*blockDim.x+threadIdx.x;
 int ly=blockIdx.y*blockDim.y+threadIdx.y;
 if(lx>=p.dstW||ly>=p.dstH)return;
 const int x=lx+p.dstX,y=ly+p.dstY;
 Rgba src{0,0,0,0};
 if(p.src)src=source(p,x,y);
 Rgba out{0,0,0,0};
 if(!(p.flags&8)){
  const Rgba g=sample(p,coordinate(p,x,y,src));
  if constexpr(Fast){
   const float a=unit(g.a)*(p.alpha==0?1.f:unit(src.a));
   out={g.r*a,g.g*a,g.b*a,a};
  }else out=compose(p,src,g,x,y);
 }
 auto* row=(float4*)((char*)p.dst+(long long)ly*p.dstPitch);
 row[lx]=make_float4(out.b,out.g,out.r,out.a);
}
extern "C" __global__ void gradation_fast(Params p){pixel<true>(p);}
extern "C" __global__ void gradation_general(Params p){pixel<false>(p);}
