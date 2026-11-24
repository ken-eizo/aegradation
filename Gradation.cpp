#define PF_DEEP_COLOR_AWARE 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectUI.h"
#include "AE_EffectSuites.h"
#include "AE_AdvEffectSuites.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectPixelFormat.h"
#include "AE_GeneralPlug.h" // AEGP_UtilitySuite6::AEGP_GetMainHWND (official host HWND)
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_PluginData.h"
#include "AEFX_SuiteHandlerTemplate.h"
#include <adobesdk/DrawbotSuite.h>
#include "gradient_core.hpp"
#include "stop_interpolation.hpp"
#include "raster_core.hpp"
#include "raster_parallel.hpp"
#include "raster_horizontal32.hpp"
#include "lut_cache.hpp"
#include "ae_compute_lut_cache.hpp"
#include "raster_angle_cache.hpp"
#include "raster_geometry_cache.hpp"
#include "gradation_cuda_params.hpp"
#if defined(_WIN32)
#include "gradation_cuda_host.hpp"
namespace gpu_backend=gradation::cuda_render;
#elif defined(__APPLE__)
#include "gradation_metal_host.hpp"
namespace gpu_backend=gradation::metal_render;
#endif
#include "stop_rgba_dialog_embedded.hpp"
#include "gradient_custom_ui_layout.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <memory>
#include <new>
#include <chrono>
#include <cstdlib>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#endif
using namespace gradation;
namespace {
// Optional profiling controlled by GRADATION_PROFILE=1 before AE starts.
// Normal releases perform NO timer syscalls, I/O or debug printing.
static void debugLog(const char* message) noexcept {
#if defined(_WIN32)
 OutputDebugStringA(message);
#else
 std::fputs(message,stderr);
#endif
}
static bool perfEnabled(){
 static const bool enabled=[](){const char* v=std::getenv("GRADATION_PROFILE");return v&&v[0]=='1';}();
 return enabled;
}
struct PerfScope {
 const char* label;
 std::chrono::steady_clock::time_point begin{};
 bool active=false;
 explicit PerfScope(const char* name):label(name),active(perfEnabled()){
  if(active)begin=std::chrono::steady_clock::now();
 }
 ~PerfScope(){
  if(!active)return;
  const double ms=std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-begin).count();
  char message[180]{};
  std::snprintf(message,sizeof(message),"Gradation profile | %s | %.3f ms\n",label,ms);
  debugLog(message);
 }
};
constexpr uint32_t MAGIC=0x47524432u;
// Positive, unique per Smart PreRender request as required by Effect SDK.
constexpr A_long SOURCE_PIXELS_ID=1;
constexpr A_long EMPTY_INPUT_ID=2;
constexpr int MAX_STOPS=32;
struct StopData { float pos; float r,g,b,a; uint32_t id; };
struct StopsData { uint32_t magic; int32_t count,selected; uint32_t nextId; StopData items[MAX_STOPS]; };
// Effect Controls array order and parameter registration match exactly.
// All obsolete Path mask/project migration parameters have been deleted.
enum ParamIndex {P_INPUT=0,P_TYPE,P_START,P_END,P_OFFSET,
 P_SCALE,P_LOOP,P_REFLECT,P_INVERT,P_SPACE,P_STOPS,P_ALPHA_BLEND,
 P_COLOR_BLEND,P_STOP_PRECISION,P_RENDER_DEVICE,P_COUNT};
static_assert(P_STOPS==10&&P_STOP_PRECISION==13&&P_RENDER_DEVICE==14&&P_COUNT==15);
constexpr char SPACE_OPTIONS[]="sRGB|sRGB-Linear|(-|HSV|HSL|HSI|(-|Oklab|Oklch|(-|CMYK|YCbCr|YCgCo|(-|CIECAM02|HCT|(-|LMS|Spectral";
static Space popSpace(int i) {
  switch(i){case 2:return Space::LinearSRGB;case 4:return Space::HSV;case 5:return Space::HSL;case 6:return Space::HSI;
  case 8:return Space::Oklab;case 9:return Space::Oklch;case 11:return Space::CMYK;case 12:return Space::YCbCr;
  case 13:return Space::YCgCo;case 15:return Space::CIECAM02;case 16:return Space::HCT;case 18:return Space::LMS;
  case 19:return Space::Spectral;default:return Space::SRGB;}
}
static StopsData defaults(){
 StopsData s{};s.magic=MAGIC;s.count=2;s.selected=-1;s.nextId=3;
 s.items[0]={0,1,1,1,1,1};s.items[1]={1,0,0,0,1,2};return s;
}
static void sanitize(StopsData& s){
 if(s.magic!=MAGIC||s.count<2||s.count>MAX_STOPS){s=defaults();return;}
 for(int i=0;i<s.count;i++){
  auto& e=s.items[i]; e.pos=std::isfinite(e.pos)?clamp(e.pos):0.f;
  for(float* p:{&e.r,&e.g,&e.b,&e.a}) *p=std::isfinite(*p)?clamp(*p):0.f;
  if(e.id==0)e.id=s.nextId++;
 }
 uint32_t selectedId=(s.selected>=0&&s.selected<s.count)?s.items[s.selected].id:0;
 std::stable_sort(s.items,s.items+s.count,[](auto&a,auto&b){return a.pos<b.pos;});
 s.selected=-1;for(int i=0;i<s.count;i++)if(s.items[i].id==selectedId)s.selected=i;
 if(s.nextId<3)s.nextId=3;
}
// Only UI selection/nextId are non-animated metadata. A stop's stable ID
// is part of the interpolation semantics when counts differ; comparing it
// is essential for AE keyframe and image cache correctness.
static bool sameStopAnimationState(const StopsData& x,const StopsData& y)noexcept{
 if(x.count!=y.count)return false;
 for(int i=0;i<x.count;i++)
  if(std::memcmp(&x.items[i].pos,&y.items[i].pos,5*sizeof(float))!=0 ||
     x.items[i].id!=y.items[i].id)
   return false;
 return true;
}
static bool readStops(PF_InData* in_data,PF_ParamDef* p,StopsData& s){
 s=defaults();
 if(!p||p->param_type!=PF_Param_ARBITRARY_DATA||!p->u.arb_d.value)return false;
 auto h=p->u.arb_d.value;
 // Never trust a stale/short arbitrary-data handle from an old project or
 // corrupted parameter. PF_GET_HANDLE_SIZE is a host callback, MFR-safe.
 if(!in_data||!in_data->utils||PF_GET_HANDLE_SIZE(h)<sizeof(StopsData))return false;
 auto v=(const StopsData*)PF_LOCK_HANDLE(h);
 if(v)s=*v;PF_UNLOCK_HANDLE(h);sanitize(s);return v!=nullptr;
}
static bool writeStops(PF_InData* in_data,PF_ParamDef* p,const StopsData& s){
 if(!p||p->param_type!=PF_Param_ARBITRARY_DATA||!p->u.arb_d.value)return false;
 auto h=p->u.arb_d.value;
 if(!in_data||!in_data->utils||PF_GET_HANDLE_SIZE(h)<sizeof(StopsData))return false;
 auto v=(StopsData*)PF_LOCK_HANDLE(h);
 if(!v)return false;
 // Selecting a stop changes only the editor UI, NOT rendered pixels.
 // Suppress AE render cache invalidation unless a position or RGBA value
 // (or stop count) has actually changed. IDs and selected are UI metadata.
 bool renderChanged=v->magic!=MAGIC||v->count!=s.count;
 if(!renderChanged)for(int i=0;i<s.count;i++){
  constexpr size_t colorFields=5*sizeof(float);
  if(std::memcmp(&v->items[i].pos,&s.items[i].pos,colorFields)!=0 ||
     v->items[i].id!=s.items[i].id){
   // A stop identity change can modify *future* animated morphs even
   // when the current frame happens to have identical color/position.
   renderChanged=true;break;
  }
 }
 *v=s;
 PF_UNLOCK_HANDLE(h);
 if(renderChanged)p->uu.change_flags|=PF_ChangeFlag_CHANGED_VALUE;
 return true;
}
static PF_Err allocStops(PF_InData* in_data,PF_ArbitraryH* out,const StopsData& s){
 if(!out)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 *out=PF_NEW_HANDLE(sizeof(StopsData));if(!*out)return PF_Err_OUT_OF_MEMORY;
 auto p=(StopsData*)PF_LOCK_HANDLE(*out);if(!p){PF_DISPOSE_HANDLE(*out);*out=nullptr;return PF_Err_INTERNAL_STRUCT_DAMAGED;}
 *p=s;PF_UNLOCK_HANDLE(*out);return PF_Err_NONE;
}
static PF_Err arb(PF_InData* in_data,PF_ArbParamsExtra* e){
 if(!e||!in_data||!in_data->utils)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 switch(e->which_function){
 case PF_Arbitrary_NEW_FUNC:return allocStops(in_data,e->u.new_func_params.arbPH,defaults());
 case PF_Arbitrary_DISPOSE_FUNC:if(e->u.dispose_func_params.arbH)PF_DISPOSE_HANDLE(e->u.dispose_func_params.arbH);return PF_Err_NONE;
 case PF_Arbitrary_COPY_FUNC:{
  // Adobe official Effect SDK guide: COPY must allocate a new arb instance
  // and return it via dst_arbPH; its incoming value is not an owned object.
  const PF_ArbitraryH source=e->u.copy_func_params.src_arbH;
  StopsData value=defaults();
  if(source&&PF_GET_HANDLE_SIZE(source)>=sizeof(StopsData)){
   const auto* ptr=(const StopsData*)PF_LOCK_HANDLE(source);
   if(!ptr)return PF_Err_OUT_OF_MEMORY;
   value=*ptr;PF_UNLOCK_HANDLE(source);
  }
  sanitize(value);
  return allocStops(in_data,e->u.copy_func_params.dst_arbPH,value);
 }
 case PF_Arbitrary_FLAT_SIZE_FUNC:
  if(!e->u.flat_size_func_params.flat_data_sizePLu)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  *e->u.flat_size_func_params.flat_data_sizePLu=sizeof(StopsData);return PF_Err_NONE;
 case PF_Arbitrary_FLATTEN_FUNC:{
  auto& a=e->u.flatten_func_params;if(!a.arbH||!a.flat_dataPV||a.buf_sizeLu<sizeof(StopsData)||
     PF_GET_HANDLE_SIZE(a.arbH)<sizeof(StopsData))return PF_Err_INTERNAL_STRUCT_DAMAGED;
  auto p=(StopsData*)PF_LOCK_HANDLE(a.arbH);if(!p)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  std::memcpy(a.flat_dataPV,p,sizeof(StopsData));PF_UNLOCK_HANDLE(a.arbH);return PF_Err_NONE;}
 case PF_Arbitrary_UNFLATTEN_FUNC:{
  auto& a=e->u.unflatten_func_params;StopsData s=defaults();
  if(a.flat_dataPV&&a.buf_sizeLu>=sizeof(StopsData))std::memcpy(&s,a.flat_dataPV,sizeof(s));
  sanitize(s);return allocStops(in_data,a.arbPH,s);}
 case PF_Arbitrary_COMPARE_FUNC:{
  auto& a=e->u.compare_func_params;StopsData x=defaults(),y=defaults();
  if(a.a_arbH&&PF_GET_HANDLE_SIZE(a.a_arbH)>=sizeof(StopsData)){
   auto p=(StopsData*)PF_LOCK_HANDLE(a.a_arbH);if(p)x=*p;PF_UNLOCK_HANDLE(a.a_arbH);
  }
  if(a.b_arbH&&PF_GET_HANDLE_SIZE(a.b_arbH)>=sizeof(StopsData)){
   auto p=(StopsData*)PF_LOCK_HANDLE(a.b_arbH);if(p)y=*p;PF_UNLOCK_HANDLE(a.b_arbH);
  }
  if(!a.compareP)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  sanitize(x);sanitize(y);
  *a.compareP=sameStopAnimationState(x,y)?
     PF_ArbCompare_EQUAL:PF_ArbCompare_NOT_EQUAL;
  return PF_Err_NONE;}
 case PF_Arbitrary_INTERP_FUNC:{
  auto& a=e->u.interp_func_params;StopsData x=defaults(),y=defaults();
  if(a.left_arbH&&PF_GET_HANDLE_SIZE(a.left_arbH)>=sizeof(StopsData)){
   auto p=(StopsData*)PF_LOCK_HANDLE(a.left_arbH);if(p)x=*p;PF_UNLOCK_HANDLE(a.left_arbH);
  }
  if(a.right_arbH&&PF_GET_HANDLE_SIZE(a.right_arbH)>=sizeof(StopsData)){
   auto p=(StopsData*)PF_LOCK_HANDLE(a.right_arbH);if(p)y=*p;PF_UNLOCK_HANDLE(a.right_arbH);
  }
  sanitize(x);sanitize(y);
  const float u=std::isfinite((float)a.tF)?
                  std::clamp((float)a.tF,0.f,1.f):0.f;
  // Stop-count changes must not switch images abruptly at t=.5.
  // Match surviving stop IDs/positions; added/deleted stops emerge from
  // or collapse into an identical ghost stop at the opposite keyframe.
  // This is independent of the Color Space popup, therefore safe even
  // for spectral/HCT interpolation and arbitrary host checkout order.
  StopsData z=defaults();
  z.count=stopmorph::interpolate(x.items,x.count,y.items,y.count,u,z.items);
  if(z.count<2)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  z.selected=-1;
  z.nextId=(uint32_t)z.count+1;
  if(u<=0.f||u>=1.f)
   z.nextId=(u<=0.f)?x.nextId:y.nextId;
  sanitize(z);
  return allocStops(in_data,a.interpPH,z);}
 case PF_Arbitrary_PRINT_SIZE_FUNC:
  if(!e->u.print_size_func_params.print_sizePLu)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  *e->u.print_size_func_params.print_sizePLu=100;return PF_Err_NONE;
 case PF_Arbitrary_PRINT_FUNC:{
  auto& a=e->u.print_func_params;if(a.print_bufferPC&&a.print_sizeLu)
   std::snprintf(a.print_bufferPC,(size_t)a.print_sizeLu,"Gradation Stops");return PF_Err_NONE;}
 case PF_Arbitrary_SCAN_FUNC:
  if(!e->u.scan_func_params.arbPH)return PF_Err_INTERNAL_STRUCT_DAMAGED;
  return allocStops(in_data,e->u.scan_func_params.arbPH,defaults());
 default:return PF_Err_NONE;
 }
}
static std::vector<Stop> getColors(const StopsData& data){
 std::vector<Stop> v;v.reserve(data.count);
 for(int i=0;i<data.count;i++){const auto&s=data.items[i];v.push_back({s.pos,{s.r,s.g,s.b,s.a}});}return v;
}
static Settings settings(PF_ParamDef* p[],int w,int h){
 Settings s;
 switch((int)p[P_TYPE]->u.pd.value){
  case 2:s.shape=Shape::Radial;break;
  case 3:s.shape=Shape::Angle;break;
  case 4:s.shape=Shape::Mapping;break;
  default:s.shape=Shape::Linear;break;
 }
 s.startX=(float)FIX_2_FLOAT(p[P_START]->u.td.x_value);
 s.startY=(float)FIX_2_FLOAT(p[P_START]->u.td.y_value);
 s.endX=(float)FIX_2_FLOAT(p[P_END]->u.td.x_value);
 s.endY=(float)FIX_2_FLOAT(p[P_END]->u.td.y_value);
 s.offsetPx=(float)p[P_OFFSET]->u.fs_d.value;
 s.scalePercent=(float)p[P_SCALE]->u.fs_d.value;
 s.loop=p[P_LOOP]->u.bd.value!=0;s.reflect=p[P_REFLECT]->u.bd.value!=0;s.invert=p[P_INVERT]->u.bd.value!=0;
 // AE expressions or corrupted project data can expose non-finite coordinates.
 // Never let NaN/Inf propagate into SIMD gather indices or pixel buffer addresses.
 auto sane=[](float v,float fallback){
  return std::isfinite(v)?std::clamp(v,-100000000.f,100000000.f):fallback;
 };
 s.startX=sane(s.startX,0.f);s.startY=sane(s.startY,0.f);
 s.endX=sane(s.endX,1.f);s.endY=sane(s.endY,0.f);
 s.offsetPx=sane(s.offsetPx,0.f);
 s.scalePercent=std::clamp(sane(s.scalePercent,100.f),.01f,100000.f);
 s.space=popSpace((int)p[P_SPACE]->u.pd.value);return s;
}
static float lum(float r,float g,float b){return clamp(.2126f*r+.7152f*g+.0722f*b);}
static inline Color lutSample(const std::array<Color,4097>& lut,float t){
 float u=clamp(t)*4096.f;int i=std::min(4095,(int)u);float f=u-i;
 const auto&a=lut[i],&b=lut[i+1];return {a.r+(b.r-a.r)*f,a.g+(b.g-a.g)*f,a.b+(b.b-a.b)*f,a.a+(b.a-a.a)*f};
}
// Only immutable, per-frame evaluated values enter MFR render threads.
// PreRender can be called without a following Render: AE owns the callback
// lifetime through delete_pre_render_data_func, never a global cache.
struct RenderState {
 Settings settings;
 StopsData stops;
 blend::Config modes;
 std::shared_ptr<lutcache::Entry> preparedLut;
 int renderDevice=1; // Auto=1, CPU=2, GPU=3 (host capability permitting).
 bool needsSource=true;
 bool transparentOutput=false;
 bool allStopsOpaque=false; // Evaluated once; reused by SmartFX metadata.
 A_long inputPixelsId=SOURCE_PIXELS_ID; // Host checkout chosen per frame.
};
static RenderState makeRenderState(PF_InData* in_data,PF_ParamDef* p[]){
 RenderState state{};
 state.settings=settings(p,0,0);
 state.renderDevice=p[P_RENDER_DEVICE]?std::clamp((int)p[P_RENDER_DEVICE]->u.pd.value,1,3):1;
 readStops(in_data,p[P_STOPS],state.stops);
 state.modes.alpha=(blend::Mode)(std::clamp((int)p[P_ALPHA_BLEND]->u.pd.value,
                                             1,blend::AlphaChoices)-1);
 state.modes.color=(blend::Mode)(std::clamp((int)p[P_COLOR_BLEND]->u.pd.value,
                                             1,blend::ColorChoices)-1);
 state.modes.frameSeed=(uint32_t)(in_data->time_step?
            in_data->current_time/in_data->time_step:in_data->current_time);
 // Constant-color ramps are independent of gradient geometry, including
 // Mapping luminance and animated Start/End. In straight sRGB every stop
 // having identical RGBA is an exact algebraic constant. Internally lower
 // all four shape selectors to a horizontal Linear generator while keeping
 // the user's actual editable parameters untouched.
 // This eliminates source RGB reads and atan2/hypot for constant Mapping,
 // Radial and Angle, and enables upstream checkout elision when the alpha
 // blend is independent of the input.
 bool uniformSRGB=state.settings.space==Space::SRGB;
 for(int i=1;i<state.stops.count&&uniformSRGB;i++){
  const auto& first=state.stops.items[0];
  const auto& other=state.stops.items[i];
  uniformSRGB=std::memcmp(&first.r,&other.r,4*sizeof(float))==0;
 }
 if(uniformSRGB){
  state.settings.shape=Shape::Linear;
  state.settings.startX=0.f;state.settings.startY=0.f;
  state.settings.endX=1.f;state.settings.endY=0.f;
  state.settings.offsetPx=0.f;state.settings.scalePercent=100.f;
  state.settings.loop=false;state.settings.reflect=false;
  state.settings.invert=false;
 }
 bool opaqueStops=true,transparentStops=true;
 for(int i=0;i<state.stops.count;i++){
  const float a=state.stops.items[i].a;
  opaqueStops &= (a==1.f);
  transparentStops &= (a==0.f);
 }
 state.allStopsOpaque=opaqueStops;
 // A fully opaque gradient makes the standard Normal compositing operation
 // source-independent. Algebraic equivalences across all 8/16/32bpc:
 //   Color Normal + opaque gradient == Color Replace.
 //   Alpha Normal + opaque gradient == Alpha Replace (output alpha=1).
 // The latter is crucial for chained AE effects: never request upstream
 // pixel tiles if neither color nor alpha depends on them.
 // Mapping still requires upstream pixels for luminance sampling.
 if(opaqueStops){
  if(state.modes.color==blend::Mode::Normal)
   state.modes.color=blend::Mode::Replace;
  if(state.modes.alpha==blend::Mode::Normal)
   state.modes.alpha=blend::Mode::Replace;
 }
 // A gradient with zero alpha in EVERY stop has zero premultiplied
 // output when the independent alpha mode is Replace or Multiply.
 // This remains true for all 14 color spaces, 4 shapes, 33 RGB blends
 // and 8/16/32bpc. Skip both upstream AE effect checkout and gradient
 // LUT/color-space preparation. Keep the full result rectangle: later
 // AE effects may generate pixels from a fully transparent input.
 // Do this before mask checkout or expensive color-space preparation.
 // A zero-alpha output is invariant under all gradient shapes.
 state.transparentOutput=transparentStops&&
    (state.modes.alpha==blend::Mode::Replace||
     state.modes.alpha==blend::Mode::Multiply);
 if(state.transparentOutput){
  state.needsSource=false;
  return state;
 }
 state.needsSource=(state.settings.shape==Shape::Mapping ||
                    state.modes.alpha!=blend::Mode::Replace ||
                    state.modes.color!=blend::Mode::Replace);
 // PreRender prepares the immutable LUT exactly once per parameter snapshot.
 // Every MFR tile references it without hashing/locking/rebuilding.
 std::array<Stop,MAX_STOPS> keyStops{};
 for(int i=0;i<state.stops.count;i++){
  const auto& e=state.stops.items[i];
  keyStops[(size_t)i]={e.pos,{e.r,e.g,e.b,e.a}};
 }
 {
  PerfScope lookupTimer("LUT shared cache lookup/preparation");
  // sRGB 2/3-stop affine gradients in 32bpc need no 4097-entry LUT
  // whatsoever. Obtain only a stable cache snapshot here. RenderPrepared
  // decides whether it needs to prepare the LUT once depth/blend are known.
  // 8/16bpc packed paths keep the existing eager preparation semantics.
  bool directCandidate=state.settings.space==Space::SRGB&&
    (state.stops.count==2||state.stops.count==3)&&
    state.stops.items[0].pos==0.f&&
    state.stops.items[state.stops.count-1].pos==1.f&&
    (state.stops.count==2||
     (state.stops.items[1].pos>0.f&&state.stops.items[1].pos<1.f));
  state.preparedLut=hostcache::get(in_data?in_data->pica_basicP:nullptr,
    lutcache::makeKey(state.settings.space,keyStops.data(),
                      (uint32_t)state.stops.count),
    !directCandidate);
 }
 return state;
}
static void destroyRenderState(void* p){
 delete static_cast<RenderState*>(p);
}
static PF_Err renderPrepared(PF_InData* in_data,
 const PF_LayerDef* in,PF_LayerDef* out,
 const RenderState& state,raster::Depth depth){
 raster::parallel::HostRenderScope hostRenderScope;
 PerfScope renderTimer("CPU Render Prepared");
 if(!out||!out->data)return PF_Err_NONE;
 if(state.transparentOutput){
  const int bpp=depth==raster::Depth::F32?16:
                 depth==raster::Depth::U16?8:4;
  if((int64_t)std::abs((int64_t)out->rowbytes)<
     (int64_t)out->width*bpp||out->width<0||out->height<0)
   return PF_Err_INTERNAL_STRUCT_DAMAGED;
  const size_t n=(size_t)out->width*(size_t)bpp;
  // A tightly packed transparent world is one contiguous region.
  // Use the CRT bulk zero kernel rather than per-row calls.
  if(out->rowbytes>0&&size_t(out->rowbytes)==n&&
     uint64_t(n)*uint64_t(out->height)<=SIZE_MAX)
   std::memset(out->data,0,n*size_t(out->height));
  else for(int y=0;y<out->height;y++)
   std::memset((uint8_t*)out->data+(std::ptrdiff_t)y*out->rowbytes,0,n);
  return PF_Err_NONE;
 }
 // The compositor treats a missing or empty source as transparent black.
 const PF_LayerDef emptySource{};
 if(!in)in=&emptySource;
 const StopsData& data=state.stops;
 const Settings& s=state.settings;
 blend::Config effective=state.modes;
 // Normal color is equivalent to Replace color if the foreground gradient
 // is fully opaque. Preserve alpha semantics, including source alpha.
 if(effective.color==blend::Mode::Normal){
  bool opaque=true;
  for(int i=0;i<data.count;i++)opaque &= (data.items[i].a==1.f);
  if(opaque)effective.color=blend::Mode::Replace;
 }
 const bool fastGenerate=
   effective.color==blend::Mode::Replace &&
   effective.alpha==blend::Mode::Replace &&
   s.shape!=Shape::Mapping;
 const bool needsIntegerPacked=effective.legacyFast()||fastGenerate;
 // The immutable cache entry is pinned in the render-state snapshot
 // created in PreRender. All output tiles share it without any cache lock.
 const std::shared_ptr<lutcache::Entry>& cache=state.preparedLut;
 if(!cache)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 const raster::PackedRamp8* packed8=nullptr;
 const raster::PackedRamp16* packed16=nullptr;
 if(needsIntegerPacked&&depth==raster::Depth::U8)
  packed8=cache->getPacked8();
 else if(needsIntegerPacked&&depth==raster::Depth::U16)
  packed16=cache->getPacked16();
 raster::Buffer src{(const uint8_t*)in->data,
                    in->data?(int)in->width:0,in->data?(int)in->height:0,
                    in->data?(int)in->rowbytes:0,(int)in->origin_x,(int)in->origin_y};
 raster::WritableBuffer dst{(uint8_t*)out->data,(int)out->width,(int)out->height,
                    (int)out->rowbytes,(int)out->origin_x,(int)out->origin_y};
 {
  PerfScope kernelTimer("CPU raster SIMD");
  raster::DirectRamp affine{};
  const raster::DirectRamp* direct=nullptr;
  // Exact two-stop sRGB gradient: avoid random float-LUT gathers in 32bpc.
  // Other colorspaces, nonuniform stop positions and >2 stops use the LUT.
#if defined(__AVX2__)
  const bool directAllowed=depth==raster::Depth::F32 ||
       (depth==raster::Depth::U16&&!effective.legacyFast()&&!fastGenerate)||
       (depth==raster::Depth::U8&&effective.color==blend::Mode::Normal&&
          (effective.alpha==blend::Mode::Multiply||
           effective.alpha==blend::Mode::Normal||
           effective.alpha==blend::Mode::Replace));
#else
  const bool directAllowed=false;
#endif
  // In measured 1080p F32 Replace+Multiply cases a segmented
  // 3-stop sampler was slower than the packed/LUT path for Radial and
  // Mapping. Do not regress the high-frequency simple alpha compositor.
  const bool threeStopPoorFit=(data.count==3&&depth==raster::Depth::F32&&
     effective.legacyFast()&&
     (s.shape==Shape::Radial||s.shape==Shape::Mapping));
  if(directAllowed&&!threeStopPoorFit&&s.space==Space::SRGB&&
     (data.count==2||data.count==3)&&
     data.items[0].pos==0.f&&data.items[data.count-1].pos==1.f&&
     (data.count==2||
      (data.items[1].pos>0.f&&data.items[1].pos<1.f))){
   const auto& lo=data.items[0];
   const auto& hi=data.items[data.count-1];
   affine.first={lo.r,lo.g,lo.b,lo.a};
   affine.last={hi.r,hi.g,hi.b,hi.a};
   if(data.count==3){
    const auto& mid=data.items[1];
    affine.middle={mid.r,mid.g,mid.b,mid.a};
    affine.middlePosition=mid.pos;
    affine.count=3;
   }
   direct=&affine;
  }
  // Any path without a complete 32bpc affine fast sampler must resolve
  // the deferred LUT before rendering. Packed integer LUT accessors
  // already guarantee preparation, but 8/16 general blends use floats.
  if(depth!=raster::Depth::F32||!direct){
   PerfScope prepareTimer("LUT deferred prepare if cache miss");
   cache->prepare();
  }
  // Full-frame F32 heavy operations may split independent output rows
  // across a bounded set of CPU cores; small AE tiles continue with zero
  // threading overhead. At most one render claims additional CPU workers,
  // preserving native AE MFR concurrency and effect stack composability.
  // Direct 32f renders do not touch the shared Entry::ramp pointer:
  // another concurrent AE MFR frame may initialize that pointer lazily.
  if(perfEnabled()){
   char message[240]{};
   std::snprintf(message,sizeof(message),
    "Gradation profile | tile=%dx%d origin=%d,%d bpc=%d shape=%d colorMode=%d alphaMode=%d input=%d direct=%d\n",
    dst.width,dst.height,dst.originX,dst.originY,
    depth==raster::Depth::F32?32:depth==raster::Depth::U16?16:8,
    (int)s.shape,(int)effective.color,(int)effective.alpha,
    src.data?1:0,direct?1:0);
   debugLog(message);
  }
  const raster::Ramp& rampRef=(depth==raster::Depth::F32&&direct)?
       lutcache::unusedDirectRamp():cache->preparedRamp();
  // For small AE SmartFX tiles, a horizontal 32f gradient is identical
  // on every source row. Sample it once, then multiply only the source
  // alpha on each row. On 320x128 32bpc this is ~2.3x faster while every
  // float channel is byte-identical. Full-frame 1080p can regress on
  // memory bandwidth, so this specialization is strictly tile-scoped.
  const bool horizontalTile=depth==raster::Depth::F32&&dst.width>=128&&
     raster::horizontal32::eligible(src,dst,s,effective);
#if defined(__AVX2__)
  if(raster::anglecache::render(raster::anglecache::global(),cache,src,dst,
       depth,s,rampRef,packed8,packed16,effective))
   return PF_Err_NONE;
  if(raster::geometrycache::render(raster::geometrycache::global(),src,dst,
       depth,s,packed8,effective))
   return PF_Err_NONE;
#endif
  if(horizontalTile){
   if(!raster::horizontal32::render(src,dst,s,rampRef,direct))
    return PF_Err_INTERNAL_STRUCT_DAMAGED;
  }else if(!raster::parallel::render(src,dst,depth,s,rampRef,
                                    packed8,packed16,effective,direct))
   return PF_Err_INTERNAL_STRUCT_DAMAGED;
 }
 return PF_Err_NONE;
}
static PF_Err render(PF_InData* in_data,PF_ParamDef* p[],PF_LayerDef* out){
 if(!in_data||!p||!out)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 const RenderState state=makeRenderState(in_data,p);
 PF_PixelFormat fmt=PF_PixelFormat_ARGB32;
 if(in_data->pica_basicP){
  AEFX_SuiteScoper<PF_WorldSuite2,TRUE> world(
    in_data,kPFWorldSuite,kPFWorldSuiteVersion2,nullptr);
  if(world.get()&&world->PF_GetPixelFormat)
   (void)world->PF_GetPixelFormat((PF_EffectWorld*)out,&fmt);
 }
 raster::Depth depth=(fmt==PF_PixelFormat_ARGB128)?raster::Depth::F32:
              (PF_WORLD_IS_DEEP(out)?raster::Depth::U16:raster::Depth::U8);
 return renderPrepared(in_data,&p[P_INPUT]->u.ld,out,state,depth);
}
// Figma export: 440x119 physical pixels on a 3840x2160, 150%-scaled
// display. AE's PF_PUI_CONTROL takes LOGICAL units and applies host DPI.
// Keep the same fraction of the monitor on FHD/4K; don't hardcode 440px.
static customui::Size nativeGradientControlSize() noexcept {
#if defined(_WIN32)
 HWND window=GetForegroundWindow();
 DWORD pid=0;
 if(window)GetWindowThreadProcessId(window,&pid);
 if(pid!=GetCurrentProcessId())window=nullptr;
 const HMONITOR monitor=window
  ?MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY)
  :MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
 MONITORINFO info{};info.cbSize=sizeof(info);
 int mw=3840,mh=2160;
 if(GetMonitorInfoW(monitor,&info)){
  mw=info.rcMonitor.right-info.rcMonitor.left;
  mh=info.rcMonitor.bottom-info.rcMonitor.top;
 }
 const UINT dpi=window?GetDpiForWindow(window):GetDpiForSystem();
 return customui::requestedLogicalSize(mw,mh,int(dpi?dpi:96));
#else
 // AppKit logical points; AE owns the Retina backing-scale conversion.
 return customui::requestedLogicalSize(3840,2160,144);
#endif
}
static PF_Err paramsSetup(PF_InData* in_data,PF_OutData* out_data){
 PF_Err err=PF_Err_NONE;PF_ParamDef def;AEFX_CLR_STRUCT(def);
 def.flags=PF_ParamFlag_SUPERVISE;
 PF_ADD_POPUP("Gradient Type",4,1,
              "Linear|Radial|Angle|Mapping",1);
 AEFX_CLR_STRUCT(def);PF_ADD_POINT("Start",25,50,FALSE,2);
 AEFX_CLR_STRUCT(def);PF_ADD_POINT("End",75,50,FALSE,3);
 AEFX_CLR_STRUCT(def);PF_ADD_FLOAT_SLIDERX("Offset (px)",-100000,100000,-2000,2000,0,PF_Precision_TENTHS,0,0,4);
 AEFX_CLR_STRUCT(def);PF_ADD_FLOAT_SLIDERX("Scale (%)",0.01f,100000,1,1000,100,PF_Precision_TENTHS,0,0,5);
 AEFX_CLR_STRUCT(def);PF_ADD_CHECKBOX("Loop","",FALSE,0,6);
 AEFX_CLR_STRUCT(def);PF_ADD_CHECKBOX("Reflect","",FALSE,0,7);
 AEFX_CLR_STRUCT(def);PF_ADD_CHECKBOX("Invert","",FALSE,0,8);
 AEFX_CLR_STRUCT(def);PF_ADD_POPUP("Color Space",19,1,SPACE_OPTIONS,9);
 PF_Handle h=PF_NEW_HANDLE(sizeof(StopsData));if(!h)return PF_Err_OUT_OF_MEMORY;
 auto v=(StopsData*)PF_LOCK_HANDLE(h);
 if(!v){PF_DISPOSE_HANDLE(h);return PF_Err_OUT_OF_MEMORY;}
 *v=defaults();PF_UNLOCK_HANDLE(h);
 // PF_ADD_ARBITRARY2 would return immediately on failed add_param before
 // allowing us to reclaim h. Explicit registration preserves the exact
 // SDK definition while preventing an orphaned default handle.
 AEFX_CLR_STRUCT(def);
 def.param_type=PF_Param_ARBITRARY_DATA;
 def.flags=PF_ParamFlag_SUPERVISE;
 def.ui_flags=PF_PUI_CONTROL;
 const auto customSize=nativeGradientControlSize();
 def.ui_width=(A_short)customSize.width;
 def.ui_height=(A_short)customSize.height;
 PF_STRNNCPY(def.name,"Gradient",sizeof(def.name));
 def.u.arb_d.value=nullptr;
 def.u.arb_d.pad=0;
 def.u.arb_d.dephault=h;
 def.u.arb_d.refconPV=nullptr;
 def.uu.id=def.u.arb_d.id=10;
 err=PF_ADD_PARAM(in_data,-1,&def);
 if(err!=PF_Err_NONE){
  PF_DISPOSE_HANDLE(h);
  return err;
 }
 // Independent color and alpha blending modes.
 AEFX_CLR_STRUCT(def);PF_ADD_POPUP("Alpha Blend Mode",blend::AlphaChoices,6,blend::AlphaOptions,11);
 AEFX_CLR_STRUCT(def);PF_ADD_POPUP("Color Blend Mode",blend::ColorChoices,2,blend::ColorOptions,12);
 // PF effect callbacks cannot read AE project bit depth from UI events.
 // Formatting is user selectable; default 32-bit preserves full float RGBA.
 AEFX_CLR_STRUCT(def);
 PF_ADD_POPUP("Stop Edit Precision",3,1,
              "32bpc Float (0-1)|8bpc Integer (0-255)|16bpc Integer (0-32768)",13);
 AEFX_CLR_STRUCT(def);
 def.flags=PF_ParamFlag_CANNOT_TIME_VARY;
 PF_ADD_POPUP("Render Device",3,1,"Auto|CPU|GPU",14);
 out_data->num_params=P_COUNT;
 if(!err&&in_data->inter.register_ui){PF_CustomUIInfo u;AEFX_CLR_STRUCT(u);u.events=PF_CustomEFlag_EFFECT;err=in_data->inter.register_ui(in_data->effect_ref,&u);}
 return err;
}
// PF_UpdateParamUI is the officially supported cosmetic update path for
// ui_width/ui_height. AE calls this on Effect Controls refresh; monitor
// changes can therefore update the compact reference height when ECW updates.
// Draw and hit testing use the actual frame width, not this requested width.
static PF_Err refreshCustomUiSize(PF_InData* in,PF_OutData* out,
                                 PF_ParamDef* params[]){
 if(!in||!out||!params||!params[P_STOPS]||!in->pica_basicP)
  return PF_Err_NONE;
 const auto dims=nativeGradientControlSize();
 const auto* old=params[P_STOPS];
 if(old->ui_width==dims.width&&old->ui_height==dims.height)
  return PF_Err_NONE;
 AEFX_SuiteScoper<PF_ParamUtilsSuite3,TRUE> util(
  in,kPFParamUtilsSuite,kPFParamUtilsSuiteVersion3,out);
 if(!util.get()||!util->PF_UpdateParamUI)return PF_Err_NONE;
 PF_ParamDef updated=*old;
 updated.ui_width=(A_short)dims.width;
 updated.ui_height=(A_short)dims.height;
 return util->PF_UpdateParamUI(in->effect_ref,P_STOPS,&updated);
}
static PF_Err global(PF_OutData* o){
 // New parameter layout and four-shape renderer invalidate old AE caches.
 o->my_version=PF_VERSION(1,0,0,PF_Stage_RELEASE,4);
 o->out_flags=PF_OutFlag_DEEP_COLOR_AWARE|PF_OutFlag_PIX_INDEPENDENT|
              PF_OutFlag_CUSTOM_UI|PF_OutFlag_SEND_UPDATE_PARAMS_UI;
 // Replace/Normal alpha can reveal an image from transparent input.
 // Prevent AE's upstream-alpha trimming from hiding generated pixels.
 o->out_flags2=PF_OutFlag2_SUPPORTS_THREADED_RENDERING|
               PF_OutFlag2_SUPPORTS_SMART_RENDER|
               PF_OutFlag2_FLOAT_COLOR_AWARE|
               PF_OutFlag2_REVEALS_ZERO_ALPHA|PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
 return PF_Err_NONE;
}
static PF_Err preRender(PF_InData* in,PF_PreRenderExtra* ex){
 PerfScope preTimer("Smart PreRender including parameter evaluation");
 if(!ex||!ex->input||!ex->cb||!ex->output)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 PF_CheckoutResult res{};
 const PF_RenderRequest req=ex->input->output_request;
 PF_Err err=PF_Err_NONE;
 // Official SmartFX pre_render_data: AE owns and destroys the snapshot.
 // The following parameter checkouts are done ONCE per PreRender, not once
 // per concurrent tile during SmartRender. The GUI-only precision param
 // P_STOP_PRECISION is not part of rendering and is never checked out.
 PF_ParamDef checked[P_COUNT]{};
 PF_ParamDef* values[P_COUNT]{};
 struct ParamScope {
  PF_InData* in=nullptr;
  PF_ParamDef* params=nullptr;
  std::array<int,P_COUNT> acquired{};
  int count=0;
  ~ParamScope(){
   for(int j=count-1;j>=0;j--)PF_CHECKIN_PARAM(in,&params[acquired[j]]);
  }
 } scope{in,checked};
 {
  PerfScope paramsTimer("SDK PF_CHECKOUT_PARAM selected parameters");
  // Exclude precision: changes the UI formatting, never image pixels.
  for(int i=1;i<P_COUNT;i++){
   if(i==P_STOP_PRECISION)continue;
   AEFX_CLR_STRUCT(checked[i]);
   err=PF_CHECKOUT_PARAM(in,i,in->current_time,in->time_step,
                         in->time_scale,&checked[i]);
   if(err!=PF_Err_NONE)return err;
   scope.acquired[scope.count++]=i;
   values[i]=&checked[i];
  }
 }
 std::unique_ptr<RenderState> state(new(std::nothrow) RenderState);
 if(!state)return PF_Err_OUT_OF_MEMORY;
 {
  PerfScope stateTimer("Snapshot / LUT lookup / gradient normalization");
  *state=makeRenderState(in,values);
 }
 // An Effect Plugin has an input even when its output is an independent
 // generator. Declare one SDK input dependency on every PreRender so that
 // checkout_output is legal. A zero-size request obtains bounds metadata
 // without forcing earlier effects to render any pixels.
 PF_RenderRequest inputRequest=req;
 if(!state->needsSource)inputRequest.rect={0,0,0,0};
 {
  PerfScope inputTimer("SDK checkout_layer bounds / dependency");
  err=ex->cb->checkout_layer(in->effect_ref,0,SOURCE_PIXELS_ID,
                            &inputRequest,in->current_time,
                            in->time_step,in->time_scale,&res);
 }
 if(err!=PF_Err_NONE)return err;
 if(state->needsSource){
  // Host reports a solid upstream alpha over its result rectangle.
  // Replace/fully opaque Normal RGB is independent of input colors.
  // With Alpha Multiply, multiplying by source alpha=1 is redundant.
  const bool replacedColor=
     state->modes.color==blend::Mode::Replace ||
     (state->modes.color==blend::Mode::Normal&&state->allStopsOpaque);
  const PF_LRect& r=res.result_rect;
  const bool sourceFullyCoversRequest=
      r.left<=req.rect.left&&r.top<=req.rect.top&&
      r.right>=req.rect.right&&r.bottom>=req.rect.bottom;
  if(res.solid&&sourceFullyCoversRequest&&
     replacedColor&&state->modes.alpha==blend::Mode::Multiply&&
     state->settings.shape!=Shape::Mapping){
   state->modes.alpha=blend::Mode::Replace;
   state->needsSource=false;
   // The full ROI checkout above was only needed for its solid hint.
   // Declare a distinct empty dependency for SmartRender, so the host
   // never has to materialize upstream pixels just to permit output.
   PF_RenderRequest emptyRequest=req;
   emptyRequest.rect={0,0,0,0};
   PF_CheckoutResult emptyResult{};
   err=ex->cb->checkout_layer(in->effect_ref,0,EMPTY_INPUT_ID,
                            &emptyRequest,in->current_time,
                            in->time_step,in->time_scale,&emptyResult);
   if(err!=PF_Err_NONE)return err;
   state->inputPixelsId=EMPTY_INPUT_ID;
  }
 }
 // SmartFX caching contract:
 // max_result_rect is a property of the effect state and its upstream
 // graph, NEVER the current tile/request. result_rect, in contrast, is
 // the intersection of request and true support. In particular, an
 // empty bounds-only request still advertises an identical maximum.
 const auto empty=[](const PF_LRect& r) noexcept {
  return r.right<=r.left||r.bottom<=r.top;
 };
 const auto intersect=[&](const PF_LRect& a,const PF_LRect& b) noexcept {
  PF_LRect r{};
  r.left=std::max(a.left,b.left);r.top=std::max(a.top,b.top);
  r.right=std::min(a.right,b.right);
  r.bottom=std::min(a.bottom,b.bottom);
  if(empty(r))return PF_LRect{};
  return r;
 };
 PF_LRect maximum{};
 if(state->transparentOutput){
  // Mathematically zero RGBA everywhere: do not cache or render
  // imaginary full-frame output. Downstream generators declare their
  // own support and are not dependent on this empty support.
  maximum=PF_LRect{};
 }else if(state->modes.alpha==blend::Mode::Multiply&&
          state->needsSource&&!req.preserve_rgb_of_zero_alpha){
  // Multiply cannot reveal alpha outside the upstream support. Keep
  // its maximum from AE's full-source bounds, not requested ROI.
  maximum=res.max_result_rect;
 }else{
  // An alpha-producing gradient may reveal any pixel of the layer and
  // preceding effects may have expanded its extent beyond this layer.
  maximum.left=0;maximum.top=0;
  maximum.right=std::max(0L,(long)in->width);
  maximum.bottom=std::max(0L,(long)in->height);
  const auto& m=res.max_result_rect;
  if(!empty(m)){
   maximum.left=std::min(maximum.left,m.left);
   maximum.top=std::min(maximum.top,m.top);
   maximum.right=std::max(maximum.right,m.right);
   maximum.bottom=std::max(maximum.bottom,m.bottom);
  }
 }
 if(empty(maximum))maximum=PF_LRect{};
 ex->output->max_result_rect=maximum;
 const bool restrictToSource=state->modes.alpha==blend::Mode::Multiply&&
   state->needsSource&&!req.preserve_rgb_of_zero_alpha;
 ex->output->result_rect=intersect(req.rect,
    restrictToSource?res.result_rect:maximum);
 // Advertise accurate full-alpha coverage to downstream AE effects:
 // when every gradient stop has alpha=1 and Alpha Replace/Normal is used,
 // output alpha is guaranteed opaque. A previously solid input also
 // qualifies for Alpha Multiply after it is algebraically specialized
 // to Replace. Never mark translucent or clipped output as solid.
 ex->output->solid=(state->allStopsOpaque&&
    (state->modes.alpha==blend::Mode::Replace||
     state->modes.alpha==blend::Mode::Normal))?TRUE:FALSE;
 if(perfEnabled()){
  char message[280]{};
  std::snprintf(message,sizeof(message),
   "Gradation profile | pre-render frame=%ld/%ld req=(%ld,%ld,%ld,%ld) needsSource=%d sourceSolid=%d transparent=%d bounds=(%ld,%ld,%ld,%ld)\n",
   (long)in->current_time,(long)in->time_step,
   (long)req.rect.left,(long)req.rect.top,
   (long)req.rect.right,(long)req.rect.bottom,
   state->needsSource?1:0,res.solid?1:0,
   state->transparentOutput?1:0,
   (long)ex->output->result_rect.left,
   (long)ex->output->result_rect.top,
   (long)ex->output->result_rect.right,
   (long)ex->output->result_rect.bottom);
  debugLog(message);
 }
 ex->output->flags&=~PF_RenderOutputFlag_GPU_RENDER_POSSIBLE;
 if(state->renderDevice!=2&&gpu_backend::supports(ex->input->what_gpu)&&ex->input->gpu_data&&ex->input->bitdepth==32)
  ex->output->flags|=PF_RenderOutputFlag_GPU_RENDER_POSSIBLE;
 ex->output->pre_render_data=state.release();
 ex->output->delete_pre_render_data_func=&destroyRenderState;
 return PF_Err_NONE;
}
static PF_Err smartRender(PF_InData* in,PF_SmartRenderExtra* ex,bool gpu=false){
 PerfScope smartTimer("Smart Render including input/output checkout");
 if(!in||!ex||!ex->cb||!ex->input||!ex->input->pre_render_data)
  return PF_Err_BAD_CALLBACK_PARAM;
 const auto* snapshot=static_cast<const RenderState*>(
      ex->input->pre_render_data);
 if(snapshot->inputPixelsId<=0||(gpu&&snapshot->renderDevice==2))return PF_Err_BAD_CALLBACK_PARAM;
 // SmartFX requires the input to be checked out before output; when
 // the result is input-independent this ID names an EMPTY-ROI checkout,
 // not an upstream pixel render. Keep both operations balanced even
 // if the output checkout or render fails.
 PF_EffectWorld* pixels=nullptr;
 PF_Err err=PF_Err_NONE;
 {
  PerfScope checkoutTimer("SDK checkout_layer_pixels (ROI-aware)");
  err=ex->cb->checkout_layer_pixels(
      in->effect_ref,snapshot->inputPixelsId,&pixels);
 }
 if(err!=PF_Err_NONE)return err;
 struct Checkin {
  PF_InData* in;
  PF_SmartRenderExtra* ex;
  A_long id;
  ~Checkin(){(void)ex->cb->checkin_layer_pixels(in->effect_ref,id);}
 } guard{in,ex,snapshot->inputPixelsId};
 PF_EffectWorld* output=nullptr;
 {
  PerfScope outputTimer("SDK checkout_output world");
  err=ex->cb->checkout_output(in->effect_ref,&output);
 }
 if(err!=PF_Err_NONE)return err;
 if(!output)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 const short bpc=ex->input->bitdepth;
 if(bpc!=8&&bpc!=16&&bpc!=32)return PF_Err_BAD_CALLBACK_PARAM;
 const raster::Depth depth=bpc==32?raster::Depth::F32:
                            bpc==16?raster::Depth::U16:raster::Depth::U8;
 auto* source=snapshot->needsSource?pixels:nullptr;
 if(gpu){
  using namespace gradation::cuda_render;
  if(bpc!=32)return PF_Err_BAD_CALLBACK_PARAM;
  Params p{};
  const auto& s=snapshot->settings;
  const auto& data=snapshot->stops;
  p.shape=(int)s.shape;
  p.color=(int)snapshot->modes.color;p.alpha=(int)snapshot->modes.alpha;
  p.frameSeed=snapshot->modes.frameSeed;
  p.startX=s.startX;p.startY=s.startY;p.dx=s.endX-s.startX;p.dy=s.endY-s.startY;
  p.invLen=1.f/std::max(std::hypot(p.dx,p.dy),1.e-6f);
  p.invScale=100.f/std::max(s.scalePercent,.0001f);
  p.offset=s.offsetPx*p.invLen;p.startAngle=raster::fastAtan2(p.dy,p.dx);
  p.flags=(s.loop?1:0)|(s.reflect?2:0)|(s.invert?4:0)|(snapshot->transparentOutput?8:0);
  if(s.space==Space::SRGB&&(data.count==2||data.count==3)&&
     data.items[0].pos==0.f&&data.items[data.count-1].pos==1.f&&
     (data.count==2||(data.items[1].pos>0.f&&data.items[1].pos<1.f))){
   auto rgba=[](const StopData& v){return Rgba{v.r,v.g,v.b,v.a};};
   p.directCount=data.count;p.first=rgba(data.items[0]);p.last=rgba(data.items[data.count-1]);
   if(data.count==3){p.middle=rgba(data.items[1]);p.middlePosition=data.items[1].pos;}
  }
  return gpu_backend::render(in,ex->input,source,output,p,snapshot->preparedLut);
 }
 return renderPrepared(in,(const PF_LayerDef*)source,
                       (PF_LayerDef*)output,*snapshot,depth);
}

static void bar(const PF_Rect& r,float& left,float& top,float& width,float& height){
 const auto v=customui::fit(float(r.left),float(r.top),
                             float(r.right-r.left),float(r.bottom-r.top));
 const auto b=customui::bar(v);
 left=b.x;top=b.y;width=std::max(.01f,b.w);height=std::max(.01f,b.h);
}
static bool eventCoords(PF_InData* in,PF_OutData* out,PF_EventExtra* e,float& left,float& top,float& width,float& height){
 if(!in||!out||!e||!in->pica_basicP)return false;
 const auto& w=e->effect_win;bar(w.current_frame,left,top,width,height);
 float x=(float)e->u.do_click.screen_point.h,y=(float)e->u.do_click.screen_point.v;
 auto dist=[](float x,float y,float a,float b,float w,float h){float dx=std::max({a-x,0.f,x-(a+w)});float dy=std::max({b-y,0.f,y-(b+h+20.f*h/customui::kBarH)});return dx*dx+dy*dy;};
 float ld=dist(x,y,left,top,width,height);
 AEFX_SuiteScoper<PFAppSuite6,TRUE> app(in,kPFAppSuite,kPFAppSuiteVersion6,out);
 if(auto* a=app.get()){
  PF_Point aa{(A_long)left,(A_long)top},bb{(A_long)(left+width),(A_long)(top+height)},a2{},b2{};
  if(a->PF_ConvertLocalToGlobal&&a->PF_ConvertLocalToGlobal(&aa,&a2)==PF_Err_NONE&&
     a->PF_ConvertLocalToGlobal(&bb,&b2)==PF_Err_NONE){
   float gl=(float)a2.h,gt=(float)a2.v,gw=(float)(b2.h-a2.h),gh=(float)(b2.v-a2.v);
   if(dist(x,y,gl,gt,gw,gh)<ld){left=gl;top=gt;width=gw;height=gh;}
  }
 }
 return true;
}
static void drawRect(const DRAWBOT_SupplierSuite1* supplier,const DRAWBOT_SurfaceSuite2* surface,const DRAWBOT_PathSuite1* path,
 DRAWBOT_SupplierRef sr,DRAWBOT_SurfaceRef sf,float x,float y,float w,float h,const DRAWBOT_ColorRGBA& c){
 if(w<=0||h<=0)return;
 DRAWBOT_PathRef p=nullptr;DRAWBOT_BrushRef brush=nullptr;
 if(supplier->NewPath(sr,&p)!=PF_Err_NONE||!p)return;
 DRAWBOT_RectF32 rect{x,y,w,h};path->AddRect(p,&rect);
 if(supplier->NewBrush(sr,&c,&brush)==PF_Err_NONE&&brush){
  surface->FillPath(sf,brush,p,kDRAWBOT_FillType_Winding);supplier->ReleaseObject((DRAWBOT_ObjectRef)brush);
 }
 supplier->ReleaseObject((DRAWBOT_ObjectRef)p);
}
// Query AE's live UI theme on each redraw. This follows preference changes
// without maintaining a second theme state or loading a graphics toolkit.
static customui::Theme currentAETheme(PF_InData* in,PF_OutData* out) {
 // This exact existing AE preference is confirmed in AE 2025-2027.
 // Query the LIVE in-memory preference with the official read-only suite.
 // AEGP_GetFpLong creates a default when missing, so check existence first.
 if(in&&in->pica_basicP){
  const void* raw=nullptr;
  auto* basic=in->pica_basicP;
  if(basic->AcquireSuite&&basic->ReleaseSuite&&
     basic->AcquireSuite(kAEGPPersistentDataSuite,
                         kAEGPPersistentDataSuiteVersion4,&raw)==kSPNoError&&raw){
   const auto* prefs=static_cast<const AEGP_PersistentDataSuite4*>(raw);
   AEGP_PersistentBlobH blob=nullptr;
   A_Boolean exists=FALSE;
   A_FpLong brightness=-1.;
   constexpr const char* section="Main Pref Section v2";
   constexpr const char* key="User Interface Brightness (4) [0.0..1.0]";
   const bool ok=prefs->AEGP_GetApplicationBlob&&prefs->AEGP_DoesKeyExist&&
      prefs->AEGP_GetFpLong&&
      prefs->AEGP_GetApplicationBlob(AEGP_PersistentType_MACHINE_INDEPENDENT,
                                     &blob)==A_Err_NONE&&blob&&
      prefs->AEGP_DoesKeyExist(blob,section,key,&exists)==A_Err_NONE&&exists&&
      prefs->AEGP_GetFpLong(blob,section,key,-1.,&brightness)==A_Err_NONE&&
      std::isfinite(brightness)&&brightness>=0.&&brightness<=1.;
   (void)basic->ReleaseSuite(kAEGPPersistentDataSuite,
                             kAEGPPersistentDataSuiteVersion4);
   if(ok){
    if(brightness<.2)return customui::Theme::Dark;
    if(brightness>=.65)return customui::Theme::Light;
    return customui::Theme::Mid;
   }
  }
 }
 AEFX_SuiteScoper<PFAppSuite6,TRUE> app(in,kPFAppSuite,kPFAppSuiteVersion6,out);
 if(!app.get())return customui::Theme::Dark;
 PF_App_Color background{},white{};
 bool valid=false;
 if(app->PF_AppGetBgColor&&
    app->PF_AppGetBgColor(&background)==PF_Err_NONE)valid=true;
 else if(app->PF_AppGetColor&&
         app->PF_AppGetColor(PF_App_Color_PANEL_BACKGROUND,&background)==PF_Err_NONE)
  valid=true;
 if(!valid)return customui::Theme::Dark;
 // White is queried to account for hosts using either 32768 or 65535
 // for A_u_short UI channels. Never assume 8bpc source pixels.
 unsigned maxWhite=65535;
 if(app->PF_AppGetColor&&
    app->PF_AppGetColor(PF_App_Color_PURE_WHITE,&white)==PF_Err_NONE)
  maxWhite=std::max<unsigned>(1,std::max({white.red,white.green,white.blue}));
 const float luminance=255.f*(float(background.red)+
    float(background.green)+float(background.blue))/(3.f*float(maxWhite));
 return customui::themeFromGray(luminance);
}
// Native Drawbot bezier path. Corresponds to the exact source-node SVG
// contours Vector_11_26 / Vector_45_127 / Vector_45_126 of the 440x119
// Figma exchange. No Slugia runtime or external render target is involved.
static void drawNativePin(
 const DRAWBOT_SupplierSuite1* supplier,
 const DRAWBOT_SurfaceSuite2* surface,
 const DRAWBOT_PathSuite1* path,
 DRAWBOT_SupplierRef sr,DRAWBOT_SurfaceRef sf,
 float x,float y,float z,const StopData& stop,
 const customui::Colors& palette,bool selected){
 if(z<=0.f)return;
 DRAWBOT_PathRef p=nullptr;
 if(supplier->NewPath(sr,&p)!=PF_Err_NONE||!p)return;
 const auto move=[&](float a,float b){path->MoveTo(p,x+a*z,y+b*z);};
 const auto line=[&](float a,float b){path->LineTo(p,x+a*z,y+b*z);};
 const auto cubic=[&](float a,float b,float c,float d,float e,float f){
  const DRAWBOT_PointF32 a1{x+a*z,y+b*z};
  const DRAWBOT_PointF32 a2{x+c*z,y+d*z};
  const DRAWBOT_PointF32 a3{x+e*z,y+f*z};
  path->BezierTo(p,&a1,&a2,&a3);
 };
 move(22.8256f,41.3022f);
 line(19.291f,27.164f);
 cubic(17.5236f,20.0943f,7.47642f,20.0943f,5.709f,27.164f);
 line(2.17444f,41.3022f);
 cubic(1.06993f,45.7203f,4.41144f,50.f,8.96544f,50.f);
 line(16.0346f,50.f);
 cubic(20.5886f,50.f,23.9301f,45.7203f,22.8256f,41.3022f);
 path->Close(p);
 const float a=clamp(stop.a);
 DRAWBOT_ColorRGBA fill{
  clamp(stop.r*a+palette.bg*(1.f-a)),
  clamp(stop.g*a+palette.bg*(1.f-a)),
  clamp(stop.b*a+palette.bg*(1.f-a)),1.f};
 DRAWBOT_BrushRef brush=nullptr;
 if(supplier->NewBrush(sr,&fill,&brush)==PF_Err_NONE&&brush){
  surface->FillPath(sf,brush,p,kDRAWBOT_FillType_Winding);
  supplier->ReleaseObject((DRAWBOT_ObjectRef)brush);
 }
 DRAWBOT_ColorRGBA edge{palette.markerEdge,palette.markerEdge,palette.markerEdge,1};
 DRAWBOT_PenRef pen=nullptr;
 // Figma specifies the same 2px stroke for every stop, selected or not.
 (void)selected;
 const float lineWidth=std::max(.5f,2.f*z);
 if(supplier->NewPen(sr,&edge,lineWidth,&pen)==PF_Err_NONE&&pen){
  surface->StrokePath(sf,pen,p);
  supplier->ReleaseObject((DRAWBOT_ObjectRef)pen);
 }
 supplier->ReleaseObject((DRAWBOT_ObjectRef)p);
}
static PF_Err draw(PF_InData* in,PF_OutData* out,PF_ParamDef* p[],PF_EventExtra* e){
 if(!in||!out||!p||!e||!p[P_STOPS]||!p[P_SPACE]||!in->pica_basicP)
  return PF_Err_NONE;
 if(e->evt_in_flags&PF_EI_DONT_DRAW)return PF_Err_NONE;
 AEFX_SuiteScoper<PF_EffectCustomUISuite2,TRUE> ui(in,kPFEffectCustomUISuite,kPFEffectCustomUISuiteVersion2,out);
 AEFX_SuiteScoper<DRAWBOT_DrawbotSuite1,TRUE> drawbot(in,kDRAWBOT_DrawSuite,kDRAWBOT_DrawSuite_VersionCurrent,out);
 AEFX_SuiteScoper<DRAWBOT_SupplierSuite1,TRUE> supplier(in,kDRAWBOT_SupplierSuite,kDRAWBOT_SupplierSuite_VersionCurrent,out);
 AEFX_SuiteScoper<DRAWBOT_SurfaceSuite2,TRUE> surface(in,kDRAWBOT_SurfaceSuite,kDRAWBOT_SurfaceSuite_VersionCurrent,out);
 AEFX_SuiteScoper<DRAWBOT_PathSuite1,TRUE> path(in,kDRAWBOT_PathSuite,kDRAWBOT_PathSuite_VersionCurrent,out);
 if(!ui.get()||!drawbot.get()||!supplier.get()||!surface.get()||!path.get())return PF_Err_NONE;
 DRAWBOT_DrawRef ref=nullptr;if(ui->PF_GetDrawingReference(e->contextH,&ref)!=PF_Err_NONE||!ref)return PF_Err_NONE;
 DRAWBOT_SupplierRef s=nullptr;DRAWBOT_SurfaceRef f=nullptr;
 if(drawbot->GetSupplier(ref,&s)!=PF_Err_NONE||drawbot->GetSurface(ref,&f)!=PF_Err_NONE)return PF_Err_NONE;
 const auto& frame=e->effect_win.current_frame;
 const auto viewport=customui::fit(float(frame.left),float(frame.top),
   float(frame.right-frame.left),float(frame.bottom-frame.top));
 if(viewport.scale<=0.f)return PF_Err_NONE;
 if(perfEnabled()){
  const auto bounds=customui::bar(viewport);
  char message[220]{};
  std::snprintf(message,sizeof(message),
    "Gradation profile | Gradient ECW frame=(%ld,%ld,%ld,%ld) bar=%.2fx%.2f pinScale=%.4f\n",
    (long)frame.left,(long)frame.top,(long)frame.right,(long)frame.bottom,
    double(bounds.w),double(bounds.h),double(viewport.scale));
  debugLog(message);
 }
 const auto theme=customui::colors(currentAETheme(in,out));
 // AE paints the Effect Controls background. Leave this area untouched:
 // PF_AppGetBgColor is not necessarily the actual panel surface color.
 StopsData data;readStops(in,p[P_STOPS],data);
 Settings conf;conf.space=popSpace((int)p[P_SPACE]->u.pd.value);
 Gradient gradient(conf,getColors(data));
 float x,y,w,h;bar(frame,x,y,w,h);
 // Prefer one Drawbot image instead of ~1024 Path+Brush allocations.
 // The preview buffer is small and short-lived, and its alpha is already
 // composited against the checkerboard; use opaque BGRA or ARGB layout.
 bool previewDrawn=false;
 DRAWBOT_Boolean supportsBGRA=FALSE,supportsARGB=FALSE;
 if(supplier->SupportsPixelLayoutBGRA)
  (void)supplier->SupportsPixelLayoutBGRA(s,&supportsBGRA);
 if(supplier->SupportsPixelLayoutARGB)
  (void)supplier->SupportsPixelLayoutARGB(s,&supportsARGB);
 // Do not silently truncate the preview when a wide panel exceeds 2048.
 // Ordinary screen widths use a single Drawbot image, while extremely
 // large host frames use the existing bounded path fallback.
 const bool imageFits=w<=8192.f&&h<=512.f;
 const int previewWidth=std::clamp((int)std::lround(w),1,8192);
 const int previewHeight=std::clamp((int)std::lround(h),1,512);
 if(imageFits&&(supportsBGRA||supportsARGB)){
  const bool bgra=(supportsBGRA!=FALSE);
  const DRAWBOT_PixelLayout layout=bgra?
       kDRAWBOT_PixelLayout_32BGRA_Premul:
       kDRAWBOT_PixelLayout_32ARGB_Premul;
  std::vector<uint8_t> pixels((size_t)previewWidth*previewHeight*4);
  for(int col=0;col<previewWidth;col++){
   const Color c=gradient.sample((col+.5f)/previewWidth);
   const float a=clamp(c.a);
   for(int row=0;row<previewHeight;row++){
    const float tile=std::max(1.f,10.f*viewport.scale);
    const float ck=(((int(col/tile)+int(row/tile))&1)!=0)?theme.checker1:theme.checker0;
    const uint8_t red=raster::u8(clamp(c.r*a+ck*(1.f-a)));
    const uint8_t green=raster::u8(clamp(c.g*a+ck*(1.f-a)));
    const uint8_t blue=raster::u8(clamp(c.b*a+ck*(1.f-a)));
    uint8_t* px=pixels.data()+((size_t)row*previewWidth+col)*4;
    if(bgra){px[0]=blue;px[1]=green;px[2]=red;px[3]=255;}
    else {px[0]=255;px[1]=red;px[2]=green;px[3]=blue;}
   }
  }
  DRAWBOT_ImageRef image=nullptr;
  if(supplier->NewImageFromBuffer(s,previewWidth,previewHeight,
                                   previewWidth*4,layout,pixels.data(),&image)==PF_Err_NONE&&image){
   DRAWBOT_PointF32 pt{x,y};
   previewDrawn=(surface->DrawImage(f,image,&pt,1.f)==PF_Err_NONE);
   supplier->ReleaseObject((DRAWBOT_ObjectRef)image);
  }
 }
 if(!previewDrawn){
  // Older Drawbot providers may not support image buffers: preserve a
  // path-based fallback for these hosts.
  constexpr int columns=128,rows=4;
  for(int i=0;i<columns;i++){
   const Color c=gradient.sample((i+.5f)/columns);
   const float a=clamp(c.a);
   for(int j=0;j<rows;j++){
    const int xx=(int)(w*(i+.5f)/columns);
    const int yy=(int)(h*(j+.5f)/rows);
    const float tile=std::max(1.f,10.f*viewport.scale);
    const float ck=(((int(xx/tile)+int(yy/tile))&1)!=0)?theme.checker1:theme.checker0;
    drawRect(supplier.get(),surface.get(),path.get(),s,f,
      x+w*i/columns,y+h*j/rows,w/columns+.65f,h/rows+.65f,
      {clamp(c.r*a+ck*(1.f-a)),clamp(c.g*a+ck*(1.f-a)),
       clamp(c.b*a+ck*(1.f-a)),1});
   }
  }
 }
 const float borderWidth=std::max(.5f,viewport.scale);
 drawRect(supplier.get(),surface.get(),path.get(),s,f,x,y,w,borderWidth,{.5f,.5f,.5f,.25f});
 drawRect(supplier.get(),surface.get(),path.get(),s,f,x,y+h-borderWidth,w,borderWidth,{.5f,.5f,.5f,.25f});
 for(int i=0;i<data.count;i++){
  const auto& v=data.items[i];
  const float pinX=customui::pinLeft(viewport,v.pos);
  const float pinY=viewport.y+customui::kPinY*viewport.scale;
  drawNativePin(supplier.get(),surface.get(),path.get(),s,f,
                pinX,pinY,viewport.scale,v,theme,data.selected==i);
 }
 return PF_Err_NONE;
}
// This official SDK suite is queried ONLY during AE's PF_Cmd_EVENT.
// AEGP_GetMainHWND has no plugin-ID argument: registration with AEGP is
// not necessary just to retrieve the host HWND. Do not cache the HWND.
#if defined(_WIN32)
static HWND getMainHWNDFromAE(PF_InData* in,PF_OutData* out,
                               const char*& failure) noexcept {
 failure=nullptr;
 if(!in||!in->pica_basicP){
  failure="The Effect SDK suite manager is unavailable";
  return nullptr;
 }
 try{
  AEFX_SuiteScoper<AEGP_UtilitySuite6,true> utility(
     in,kAEGPUtilitySuite,kAEGPUtilitySuiteVersion6,out);
  if(!utility.get()||!utility->AEGP_GetMainHWND){
   failure="AEGP_UtilitySuite6::AEGP_GetMainHWND is unavailable";
   return nullptr;
  }
  void* nativeWindow=nullptr;
  if(utility->AEGP_GetMainHWND(&nativeWindow)!=A_Err_NONE){
   failure="AEGP_GetMainHWND returned a host error";
   return nullptr;
  }
  HWND owner=reinterpret_cast<HWND>(nativeWindow);
  if(!owner||!IsWindow(owner)){
   failure="AEGP_GetMainHWND returned an invalid window";
   return nullptr;
  }
  DWORD pid=0;
  const DWORD ownerThread=GetWindowThreadProcessId(owner,&pid);
  if(!ownerThread||pid!=GetCurrentProcessId()){
   failure="AEGP_GetMainHWND returned a window outside the AE process";
   return nullptr;
  }
  if(ownerThread!=GetCurrentThreadId()){
   // Do not enter Windows' cross-thread owner modal loop; this risks
   // coupling input queues while AE is in its UI callback.
   failure="Effect Controls callback is on a different UI thread from AE's main window";
   return nullptr;
  }
  return owner;
 }catch(...){
  failure="Unexpected exception while obtaining AE's window";
  return nullptr;
 }
}
#endif
// The color picker must use the project's CURRENT bit depth, not a
// constant or the previous render callback's pixel format. ProjectSuite6
// is the documented read-only SDK path on the AE UI thread.
static bool getCurrentProjectDepth(PF_InData* in,PF_OutData* out,
                                   stopui::Depth& result,
                                   const char*& failure) noexcept {
 failure=nullptr;
 if(!in||!in->pica_basicP){
  failure="Project bit depth requires the AE suite manager";
  return false;
 }
 try{
  AEFX_SuiteScoper<AEGP_ProjSuite6,true> projects(
      in,kAEGPProjSuite,kAEGPProjSuiteVersion6,out);
  if(!projects.get()||!projects->AEGP_GetNumProjects||
     !projects->AEGP_GetProjectByIndex||!projects->AEGP_GetProjectBitDepth){
   failure="AE project bit-depth suite is unavailable";
   return false;
  }
  A_long count=0;
  if(projects->AEGP_GetNumProjects(&count)!=A_Err_NONE||count<1){
   failure="AE has no readable open project";
   return false;
  }
  AEGP_ProjectH project=nullptr;
  if(projects->AEGP_GetProjectByIndex(0,&project)!=A_Err_NONE||!project){
   failure="AE cannot read the current project";
   return false;
  }
  AEGP_ProjBitDepth bitDepth=AEGP_ProjBitDepth_8;
  if(projects->AEGP_GetProjectBitDepth(project,&bitDepth)!=A_Err_NONE){
   failure="AE cannot read the project's bit depth";
   return false;
  }
  switch(bitDepth){
   case AEGP_ProjBitDepth_8: result=stopui::Depth::Bpc8;return true;
   case AEGP_ProjBitDepth_16:result=stopui::Depth::Bpc16;return true;
   case AEGP_ProjBitDepth_32:result=stopui::Depth::Bpc32;return true;
   default:failure="AE reported an unsupported project bit depth";return false;
  }
 }catch(...){
  failure="Unexpected error reading AE project bit depth";
  return false;
 }
}
static void reportPickerFailure(PF_OutData* out,const char* reason) noexcept {
 if(!out||!reason||!*reason)return;
 // The official Effect SDK displays this only after we have returned
 // from PF_Cmd_EVENT; NEVER create a competing error window.
 std::snprintf(out->return_msg,sizeof(out->return_msg),
               "Gradation Color Picker: %s",reason);
 out->out_flags|=PF_OutFlag_DISPLAY_ERROR_MESSAGE;
}
// One PF_Cmd_EVENT stays active while the OS-owned modal runs. Adobe's
// Effect SDK explicitly permits changing an arb parameter in an event, and
// PFAppSuite6::PF_InvalidateRect is valid for a non-DRAW custom UI event.
// No AE messages are sent manually and no background threads touch AE data.
struct LiveStopPreview {
 PF_InData* in=nullptr;
 PF_ParamDef* param=nullptr;
 PF_EventExtra* event=nullptr;
 StopsData* data=nullptr;
 const PFAppSuite6* app=nullptr;
 PF_OutData* out=nullptr;
 const PF_AdvAppSuite2* refresh=nullptr;
 const PF_AdvItemSuite1* item=nullptr;
 int index=-1;
 bool updated=false;
};
static void onColorPickerPreview(void* opaque,const stopui::RGBA& value) noexcept {
 auto* ctx=static_cast<LiveStopPreview*>(opaque);
 if(!ctx||!ctx->in||!ctx->param||!ctx->data||ctx->index<0||
    ctx->index>=ctx->data->count)return;
 auto& s=ctx->data->items[ctx->index];
 const float r=clamp(value.r),g=clamp(value.g);
 const float b=clamp(value.b),a=clamp(value.a);
 if(s.r==r&&s.g==g&&s.b==b&&s.a==a)return;
 const auto original=s;
 s.r=r;s.g=g;s.b=b;s.a=a;
 if(!writeStops(ctx->in,ctx->param,*ctx->data)){s=original;return;}
 ctx->updated=true;
 // The ARB handle is unlocked before calling the host. Changed-value
 // and FORCE_RERENDER are consumed by AE at event return; the documented
 // advanced suites also request the active composition's refresh NOW.
 // No timeline movement, stream writes, private APIs or render threads.
 if(ctx->out)ctx->out->out_flags|=PF_OutFlag_FORCE_RERENDER;
 if(ctx->event){
  ctx->event->evt_out_flags|=PF_EO_HANDLED_EVENT|PF_EO_UPDATE_NOW;
  if(ctx->app&&ctx->app->PF_InvalidateRect&&ctx->event->contextH&&
     *ctx->event->contextH){
   // Requests host custom-control redraw at idle, without recursive
   // PF_Cmd_EVENT invocation from the picker code itself.
   (void)ctx->app->PF_InvalidateRect(ctx->event->contextH,nullptr);
  }
 }
 if(ctx->item&&ctx->item->PF_TouchActiveItem)
  (void)ctx->item->PF_TouchActiveItem();
 if(ctx->refresh&&ctx->refresh->PF_RefreshAllWindows)
  (void)ctx->refresh->PF_RefreshAllWindows();
}
// Custom Figma SlugUI dialog is launched from this official SDK event.
// Win32 DialogBoxIndirectParamW owns modal state and the message loop.
static PF_Err event(PF_InData* in,PF_OutData* out,PF_ParamDef* p[],PF_EventExtra* e){
 if(!in||!out||!p||!p[P_STOPS]||!e||!e->contextH||!*e->contextH)
  return PF_Err_NONE;
 if((*e->contextH)->magic!=PF_CONTEXT_MAGIC||(*e->contextH)->w_type!=PF_Window_EFFECT)return PF_Err_NONE;
 if(e->effect_win.index!=P_STOPS||e->effect_win.area!=PF_EA_CONTROL)return PF_Err_NONE;
 if(e->e_type==PF_Event_DRAW)return draw(in,out,p,e);
 if(e->e_type!=PF_Event_DO_CLICK&&e->e_type!=PF_Event_DRAG)return PF_Err_NONE;
 float x,y,w,h;
 if(!eventCoords(in,out,e,x,y,w,h))return PF_Err_NONE;
 auto& click=e->u.do_click;
 float cx=(float)click.screen_point.h,cy=(float)click.screen_point.v;
 const float uiScale=h/customui::kBarH;
 if(e->e_type==PF_Event_DO_CLICK&&
    (cy<y-8.f*uiScale||cy>y+h+20.f*uiScale||
     cx<x-13.f*uiScale||cx>x+w+13.f*uiScale))return PF_Err_NONE;
 StopsData st;readStops(in,p[P_STOPS],st);
 if(e->e_type==PF_Event_DRAG){
  uint32_t id=(uint32_t)click.continue_refcon[0];if(id==0)return PF_Err_NONE;
  for(int i=0;i<st.count;i++)if(st.items[i].id==id){
   const float next=clamp((cx-x)/w);
   if(st.items[i].pos!=next||st.selected!=i){
    st.items[i].pos=next;st.selected=i;sanitize(st);
    if(writeStops(in,p[P_STOPS],st))e->evt_out_flags|=PF_EO_UPDATE_NOW;
   }
   break;
  }
  click.send_drag=!click.last_time;
  e->evt_out_flags|=PF_EO_HANDLED_EVENT;return PF_Err_NONE;
 }
 float t=clamp((cx-x)/w);int hit=-1;float best=1.e9f;
 for(int i=0;i<st.count;i++){float dist=std::abs(st.items[i].pos-t)*w;if(dist<best&&dist<12.f*uiScale){best=dist;hit=i;}}
 bool alt=(click.modifiers&PF_Mod_OPT_ALT_KEY)!=0;
 bool dbl=click.num_clicks>=2;
 if(alt&&hit>=0&&st.count>2){
  for(int i=hit;i<st.count-1;i++)st.items[i]=st.items[i+1];
  st.count--;st.selected=-1;
 }else if(dbl&&hit>=0){
  // The AE Effect SDK event triggers a standard OS-managed modal.
  // Commit only on OK; a canceled dialog leaves RGBA unchanged.
  auto& stop=st.items[hit];
  const char* hostFailure=nullptr;
#if defined(_WIN32)
  const auto owner=getMainHWNDFromAE(in,out,hostFailure);
#else
  const auto owner=stopui::nativeOwner(&hostFailure);
#endif
  // Never treat initialization failure as a successful Cancel.
  // AE displays the error after the effect's event handler returns.
  if(!owner)reportPickerFailure(out,hostFailure);
  else{
   std::string pickerFailure;
   stopui::RGBA edited{stop.r,stop.g,stop.b,stop.a};
   const StopsData beforePicker=st; // Restore exactly on Cancel/error.
   stopui::Depth activeDepth=stopui::Depth::Bpc8;
   const char* depthFailure=nullptr;
   if(!getCurrentProjectDepth(in,out,activeDepth,depthFailure)){
    reportPickerFailure(out,depthFailure);
   }else{
    const void* appRaw=nullptr;
    SPBasicSuite* suites=in->pica_basicP;
    if(suites&&suites->AcquireSuite)
     (void)suites->AcquireSuite(kPFAppSuite,kPFAppSuiteVersion6,&appRaw);
    // Optional documented suites are acquired only while the modal owns
    // the SDK UI callback. Their RAII lifetime spans all live notifications.
    AEFX_SuiteScoper<PF_AdvAppSuite2,true> refresh(
        in,kPFAdvAppSuite,kPFAdvAppSuiteVersion2,out);
    AEFX_SuiteScoper<PF_AdvItemSuite1,true> item(
        in,kPFAdvItemSuite,kPFAdvItemSuiteVersion1,out);
    LiveStopPreview preview{
      in,p[P_STOPS],e,&st,static_cast<const PFAppSuite6*>(appRaw),
      out,refresh.get(),item.get(),hit,false};
    const bool accepted=stopui::editRGBA(
       owner,edited,activeDepth,&pickerFailure,&onColorPickerPreview,&preview,
       (int)currentAETheme(in,out));
    if(accepted){
     // Reconstruct the committed state from the immutable pre-dialog
     // snapshot. Live preview may have mutated the handle and AE may
     // have consumed its change flag during a nested modal message.
     st=beforePicker;
     auto& committed=st.items[hit];
     committed.r=clamp(edited.r);committed.g=clamp(edited.g);
     committed.b=clamp(edited.b);committed.a=clamp(edited.a);
     st.selected=hit;
     sanitize(st);
     if(!writeStops(in,p[P_STOPS],st)){
      reportPickerFailure(out,"Unable to commit the color stop parameter");
      if(appRaw&&suites&&suites->ReleaseSuite)
       (void)suites->ReleaseSuite(kPFAppSuite,kPFAppSuiteVersion6);
      return PF_Err_INTERNAL_STRUCT_DAMAGED;
     }
     if(!sameStopAnimationState(st,beforePicker)){
      p[P_STOPS]->uu.change_flags|=PF_ChangeFlag_CHANGED_VALUE;
      out->out_flags|=PF_OutFlag_FORCE_RERENDER;
     }
    }else if(preview.updated){
     // A canceled preview must not leave transient RGBA in the project.
     st=beforePicker;
     (void)writeStops(in,p[P_STOPS],st);
     // A restored frame must not retain the transient preview in AE's cache.
     p[P_STOPS]->uu.change_flags|=PF_ChangeFlag_CHANGED_VALUE;
     out->out_flags|=PF_OutFlag_FORCE_RERENDER;
     if(preview.app&&preview.app->PF_InvalidateRect&&e->contextH&&
        *e->contextH)
      (void)preview.app->PF_InvalidateRect(e->contextH,nullptr);
    }
    if(appRaw&&suites&&suites->ReleaseSuite)
     (void)suites->ReleaseSuite(kPFAppSuite,kPFAppSuiteVersion6);
    if(!pickerFailure.empty())
     reportPickerFailure(out,pickerFailure.c_str());
   }
  }
  st.selected=hit;
 }else if(dbl&&hit<0&&st.count<MAX_STOPS){
  Settings s;s.space=popSpace((int)p[P_SPACE]->u.pd.value);Gradient g(s,getColors(st));
  Color c=g.sample(t);st.items[st.count++]={t,c.r,c.g,c.b,c.a,st.nextId++};
  st.selected=st.count-1;
 }else if(hit>=0){
  st.selected=hit;click.continue_refcon[0]=(A_long)st.items[hit].id;click.send_drag=TRUE;
 }
 sanitize(st);writeStops(in,p[P_STOPS],st);
 e->evt_out_flags|=PF_EO_HANDLED_EVENT|PF_EO_UPDATE_NOW;
 return PF_Err_NONE;
}
}
extern "C" DllExport PF_Err PluginDataEntryFunction2(
 PF_PluginDataPtr in,PF_PluginDataCB2 cb,SPBasicSuite* basic,const char* host,const char* version){
 PF_Err result=PF_Err_INVALID_CALLBACK;
 result=PF_REGISTER_EFFECT_EXT2(in,cb,"Gradation","ADBE Gradation","Generate",AE_RESERVED_INFO,"EffectMain","https://www.adobe.com");
 return result;
}
extern "C" DllExport PF_Err EffectMain(
 PF_Cmd cmd,PF_InData* in,PF_OutData* out,PF_ParamDef* params[],PF_LayerDef* output,void* extra){
 try{
  switch(cmd){
   case PF_Cmd_ABOUT:std::snprintf(out->return_msg,sizeof(out->return_msg),"Gradation 1.0 - Color gradient");return PF_Err_NONE;
   case PF_Cmd_GLOBAL_SETUP:{const auto err=global(out);if(!err)hostcache::setup(in?in->pica_basicP:nullptr);return err;}
   case PF_Cmd_GLOBAL_SETDOWN:hostcache::setdown(in?in->pica_basicP:nullptr);return PF_Err_NONE;
   case PF_Cmd_PARAMS_SETUP:return paramsSetup(in,out);
   case PF_Cmd_UPDATE_PARAMS_UI:return refreshCustomUiSize(in,out,params);
   case PF_Cmd_RENDER:return render(in,params,output);
   case PF_Cmd_SMART_PRE_RENDER:return preRender(in,(PF_PreRenderExtra*)extra);
   case PF_Cmd_SMART_RENDER:return smartRender(in,(PF_SmartRenderExtra*)extra);
    case PF_Cmd_SMART_RENDER_GPU:return smartRender(in,(PF_SmartRenderExtra*)extra,true);
    case PF_Cmd_GPU_DEVICE_SETUP:return gpu_backend::setup(in,out,(PF_GPUDeviceSetupExtra*)extra);
    case PF_Cmd_GPU_DEVICE_SETDOWN:return gpu_backend::setdown(in,(PF_GPUDeviceSetdownExtra*)extra);
   case PF_Cmd_ARBITRARY_CALLBACK:return arb(in,(PF_ArbParamsExtra*)extra);
   case PF_Cmd_EVENT:return event(in,out,params,(PF_EventExtra*)extra);
   default:return PF_Err_NONE;
  }
 }catch(...){return PF_Err_INTERNAL_STRUCT_DAMAGED;}
}
