#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectUI.h"
#include "AE_EffectSuites.h"
#include <SPBasic.h>
#include "AE_PluginData.h"
#include <array>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <string>
#include <iostream>
#include "gpu_host_test_support.hpp"
namespace {
using Effect=PF_Err (*)(PF_Cmd,PF_InData*,PF_OutData*,PF_ParamDef*[],PF_LayerDef*,void*);
struct Block {unsigned char* bytes;size_t size;};
// Mirrors the original v1 Gradation arbitrary payload. Keyframe topology
// changes MUST NOT change its size/layout or old project compatibility.
struct MockStopData {float pos,r,g,b,a;uint32_t id;};
struct MockStopsData {
 uint32_t magic;
 int32_t count,selected;
 uint32_t nextId;
 MockStopData items[32];
};
static_assert(sizeof(MockStopsData)==784);

std::unordered_map<PF_Handle,Block> owned;
std::vector<PF_ParamDef> params;
int registered=0;
int injectedAddFailure=-1;
int addCount=0;
int checkedParamCount=0,checkedInParamCount=0;
std::atomic<int> checkedPixelsCount{0},checkedInPixelsCount{0};
int requestedLayerCount=0;
int boundsOnlyRequestCount=0;
int positiveSourcePixelCheckouts=0;
bool emptyCheckoutID[3]={false,false,false};
PF_EffectWorld emptyInputWorld{};
bool useNarrowInputROI=false;
bool mockOpaqueSource=false;
PF_EffectWorld fakeInputWorld{},fakeOutputWorld{};
thread_local PF_EffectWorld* threadOutput=nullptr;
PF_Err fakeCheckoutParam(PF_ProgPtr,PF_ParamIndex index,A_long,A_long,A_u_long,PF_ParamDef* out){
 if(index<1||index>(PF_ParamIndex)params.size()||!out)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 checkedParamCount++;
 *out=params[(size_t)index-1];
 return PF_Err_NONE;
}
PF_Err fakeCheckinParam(PF_ProgPtr,PF_ParamDef* p){
 if(!p)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 checkedInParamCount++;return PF_Err_NONE;
}
PF_Err fakeCheckoutLayer(PF_ProgPtr,PF_ParamIndex idx,A_long id,
 const PF_RenderRequest* request,A_long,A_long,A_u_long,PF_CheckoutResult* output){
 if(!request||!output||idx!=0||(id!=1&&id!=2))
  return PF_Err_INTERNAL_STRUCT_DAMAGED;
 const bool boundsOnly=request->rect.left==request->rect.right||
                       request->rect.top==request->rect.bottom;
 emptyCheckoutID[id]=boundsOnly;
 if(boundsOnly)boundsOnlyRequestCount++;
 else requestedLayerCount++;
 *output=PF_CheckoutResult{};
 output->result_rect=boundsOnly?PF_LRect{}:request->rect;
 output->max_result_rect={0,0,48,64};
 output->solid=mockOpaqueSource?TRUE:FALSE;
 if(useNarrowInputROI){
  output->result_rect.left=12;output->result_rect.top=10;
  output->result_rect.right=30;output->result_rect.bottom=28;
  output->max_result_rect.left=8;output->max_result_rect.top=6;
  output->max_result_rect.right=34;output->max_result_rect.bottom=32;
 }
 return PF_Err_NONE;
}
PF_Err fakeCheckoutPixels(PF_ProgPtr,A_long id,PF_EffectWorld** ptr){
 if(!ptr||(id!=1&&id!=2))return PF_Err_INTERNAL_STRUCT_DAMAGED;
 if(emptyCheckoutID[id]){
  *ptr=&emptyInputWorld; // no upstream image is materialized
 }else{
  ++checkedPixelsCount;
  ++positiveSourcePixelCheckouts;
  *ptr=&fakeInputWorld;
 }
 return PF_Err_NONE;
}
PF_Err fakeCheckinPixels(PF_ProgPtr,A_long id){
 if(id!=1&&id!=2)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 if(!emptyCheckoutID[id])++checkedInPixelsCount;
 return PF_Err_NONE;
}
PF_Err fakeCheckoutOutput(PF_ProgPtr,PF_EffectWorld** ptr){
 if(!ptr)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 *ptr=threadOutput?threadOutput:&fakeOutputWorld;return PF_Err_NONE;
}
PF_Handle mockNew(A_u_longlong count){
 if(count==0||count>1u<<25)return nullptr;
 void** h=new(std::nothrow) void*;
 if(!h)return nullptr;
 unsigned char* mem=new(std::nothrow) unsigned char[(size_t)count]();
 if(!mem){delete h;return nullptr;}
 *h=mem;owned[(PF_Handle)h]={mem,(size_t)count};
 return (PF_Handle)h;
}
void* mockLock(PF_Handle h){auto it=owned.find(h);return it==owned.end()?nullptr:it->second.bytes;}
void mockUnlock(PF_Handle){}
void mockDispose(PF_Handle h){
 auto it=owned.find(h);
 if(it!=owned.end()){
  delete[] it->second.bytes;
  delete (void**)it->first;
  owned.erase(it);
 }
}
A_u_longlong mockSize(PF_Handle h){
 auto it=owned.find(h);return it==owned.end()?0:(A_u_longlong)it->second.size;
}
PF_Err mockAdd(PF_ProgPtr,PF_ParamIndex i,PF_ParamDef* p){
 if(!p||i!=-1)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 if(++addCount==injectedAddFailure)return PF_Err_OUT_OF_MEMORY;
 params.push_back(*p);
 return PF_Err_NONE;
}
PF_Err mockRegister(PF_ProgPtr,PF_CustomUIInfo* c){
 if(!c)return PF_Err_INTERNAL_STRUCT_DAMAGED;
 registered++;return PF_Err_NONE;
}
void ensure(bool pass,const char* desc){if(!pass){std::cerr<<"FAIL: "<<desc<<"\n";std::exit(2);}}
}

int entryRegistrations=0;
A_Err registration(PF_PluginDataPtr p,const A_u_char* name,
                   const A_u_char* match,const A_u_char* category,
                   const A_u_char* callback,A_long kind,
                   A_long maj,A_long min,A_long reserved,
                   const A_u_char* url){
 ensure(p!=(PF_PluginDataPtr)nullptr,"registration context");
 ensure(name&&std::strcmp((const char*)name,"Gradation")==0,"entry name");
 ensure(match&&std::strcmp((const char*)match,"ADBE Gradation")==0,"entry match");
 ensure(category&&std::strcmp((const char*)category,"Generate")==0,"entry category");
 ensure(callback&&std::strcmp((const char*)callback,"EffectMain")==0,"entry command");
 ensure(maj==13&&min==28,"registered AE SDK versions");
 ensure(kind=='eFKT',"registered effect kind matches SDK");
 ensure(reserved==8,"reserved info matches entry.h SDK registration constant");
 ensure(url&&std::strstr((const char*)url,"adobe.com"),"registered URL");
 ++entryRegistrations;
 return 0;
}
int main(int argc,char** argv){
 ensure(argc>1,"AEX absolute path argument");
 HMODULE lib=LoadLibraryA(argv[1]);
 if(!lib){std::cerr<<"LoadLibrary FAILED: Windows error "<<GetLastError()<<"\n";return 1;}
 auto effect=(Effect)GetProcAddress(lib,"EffectMain");
 ensure(effect!=nullptr,"EffectMain exported with C ABI");
 ensure(GetProcAddress(lib,"PluginDataEntryFunction2")!=nullptr,
        "Effect PluginDataEntryFunction2 exported");
 // PiPL must be present and parseable before AE can enumerate the plugin.
 HRSRC resource=FindResourceA(lib,MAKEINTRESOURCEA(16000),"PiPL");
 ensure(resource!=nullptr,"PiPL resource 16000 present");
 const DWORD resourceBytes=SizeofResource(lib,resource);
 ensure(resourceBytes>=128,"PiPL resource substantial");
 HGLOBAL raw=LoadResource(lib,resource);
 ensure(raw!=nullptr&&LockResource(raw)!=nullptr,"PiPL locked");
 auto entry=(PluginDataEntryFunction2Ptr)GetProcAddress(lib,"PluginDataEntryFunction2");
 ensure(entry!=nullptr,"plugin data entry");
 for(const char* version:{"25.0","26.0","27.0"}){
  const A_Err ret=entry((PF_PluginDataPtr)(uintptr_t)0x1,&registration,
                        nullptr,"After Effects",version);
  ensure(ret==0,"registered once for selected AE target host");
 }
 ensure(entryRegistrations==3,"registration callback called three times total");

 _PF_UtilCallbacks callbacks{};
 callbacks.host_new_handle=&mockNew;
 callbacks.host_lock_handle=&mockLock;
 callbacks.host_unlock_handle=&mockUnlock;
 callbacks.host_dispose_handle=&mockDispose;
 callbacks.host_get_handle_size=&mockSize;
 PF_InData in{};
 in.inter.add_param=&mockAdd;
 in.inter.register_ui=&mockRegister;
 in.utils=&callbacks;
 PF_OutData out{};
 PF_Err err=effect(PF_Cmd_GLOBAL_SETUP,&in,&out,nullptr,nullptr,nullptr);
 ensure(err==PF_Err_NONE,"global setup returned success");
 ensure(out.my_version==525828u,"EffectMain PF_VERSION matches PiPL AE_Effect_Version");
 ensure(out.out_flags==0x06008400u,"PiPL GLOBAL flags exact including native UI refresh");
 ensure(out.out_flags2==0x0A001480u,"PiPL GLOBAL flags2 exact");
 std::cout<<"GLOBAL flags "<<std::hex<<out.out_flags<<", "<<out.out_flags2<<std::dec<<"\n";
 // Exercise registration repeatedly, with fake AE host handles and callbacks.
 for(int rep=0;rep<25;rep++){
  params.clear();
  registered=0;addCount=0;
  PF_OutData pd{};
  err=effect(PF_Cmd_PARAMS_SETUP,&in,&pd,nullptr,nullptr,nullptr);
  ensure(err==PF_Err_NONE,"params setup succeeds");
  ensure((size_t)pd.num_params==params.size()+1,"params count includes input layer");
  ensure(registered==1,"UI registered exactly once");
  ensure(params.size()==14,"14 declared parameters; no Mask Path or project marker");
  const std::array<A_long,14> expectedIDs{{
    1,2,3,4,5,6,7,8,9,10,11,12,13,14
  }};
  for(size_t k=0;k<params.size();k++)
   ensure(params[k].uu.id==expectedIDs[k],
          "gradient parameter IDs in established 1..14 order");
  ensure(params[0].u.pd.num_choices==4&&
         std::string(params[0].u.pd.u.namesptr)==
           "Linear|Radial|Angle|Mapping",
         "Gradient Type has exactly four options without Path");
  for(const auto& p:params)
   ensure(p.param_type!=PF_Param_PATH,"no Path parameter is registered");

  for(const auto& q:params)if(q.param_type==PF_Param_POPUP){
   ensure(q.u.pd.u.namesptr!=nullptr,"popup strings exist");
   const std::string options=q.u.pd.u.namesptr;
   ensure(options.size()<2048,"popup string is bounded");
   ensure(!options.empty()&&options.front()!='|'&&options.back()!='|',
          "popup delimiter boundaries");
   ensure(std::count(options.begin(),options.end(),'|')+1==q.u.pd.num_choices,
          "popup advertised choices equal actual menu choices");
   ensure(q.u.pd.dephault>0&&q.u.pd.dephault<=q.u.pd.num_choices,
          "popup default index within menu");
  }

  int arbcount=0;
  PF_Handle initial=nullptr;
  for(auto& param:params)if(param.param_type==PF_Param_ARBITRARY_DATA){
   arbcount++;
   initial=param.u.arb_d.dephault;
   ensure(initial!=nullptr,"default arbitrary data handle");
   ensure(mockSize(initial)>sizeof(uint32_t),"default arb handle has payload");
  }
  ensure(arbcount==1,"exactly one arb");
  auto perform=[&](PF_ArbParamsExtra& a)->PF_Err{
   return effect(PF_Cmd_ARBITRARY_CALLBACK,&in,&pd,nullptr,nullptr,&a);
  };
  PF_ArbParamsExtra data{};
  PF_Handle first=nullptr;
  data.which_function=PF_Arbitrary_COPY_FUNC;
  data.u.copy_func_params.src_arbH=initial;
  data.u.copy_func_params.dst_arbPH=&first;
  ensure(perform(data)==PF_Err_NONE&&first&&first!=initial,"arb copy allocates independent destination");
  ensure(mockSize(first)==mockSize(initial),"arb copy size");
  ensure(std::memcmp(mockLock(first),mockLock(initial),(size_t)mockSize(initial))==0,
         "arb copy preserves payload");
  PF_Handle fromEmpty=nullptr;
  data={};
  data.which_function=PF_Arbitrary_COPY_FUNC;
  data.u.copy_func_params.src_arbH=nullptr;
  data.u.copy_func_params.dst_arbPH=&fromEmpty;
  ensure(perform(data)==PF_Err_NONE&&fromEmpty,"arb null source copies defaults");
  A_u_long flatSize=0;
  data={};
  data.which_function=PF_Arbitrary_FLAT_SIZE_FUNC;
  data.u.flat_size_func_params.arbH=first;
  data.u.flat_size_func_params.flat_data_sizePLu=&flatSize;
  ensure(perform(data)==PF_Err_NONE&&flatSize==(A_u_long)mockSize(first),"arb flatten size");
  std::vector<uint8_t> flat(flatSize);
  data={};
  data.which_function=PF_Arbitrary_FLATTEN_FUNC;
  data.u.flatten_func_params.arbH=first;
  data.u.flatten_func_params.buf_sizeLu=flatSize;
  data.u.flatten_func_params.flat_dataPV=flat.data();
  ensure(perform(data)==PF_Err_NONE,"arb flatten");
  PF_Handle restored=nullptr;
  data={};
  data.which_function=PF_Arbitrary_UNFLATTEN_FUNC;
  data.u.unflatten_func_params.flat_dataPV=flat.data();
  data.u.unflatten_func_params.buf_sizeLu=flatSize;
  data.u.unflatten_func_params.arbPH=&restored;
  ensure(perform(data)==PF_Err_NONE&&restored,"arb unflatten");
  PF_ArbCompareResult comparison=PF_ArbCompare_NOT_EQUAL;
  data={};
  data.which_function=PF_Arbitrary_COMPARE_FUNC;
  data.u.compare_func_params.a_arbH=first;
  data.u.compare_func_params.b_arbH=restored;
  data.u.compare_func_params.compareP=&comparison;
  ensure(perform(data)==PF_Err_NONE&&comparison==PF_ArbCompare_EQUAL,
         "arb round-trip compare equal");

  // Exercise the REAL AEX PF_Arbitrary_INTERP_FUNC through mock AE handles.
  // New stop count 2 -> 3 must smoothly animate, not jump at 50%.
  PF_Handle rightKey=mockNew(flatSize);
  ensure(rightKey!=nullptr,"right keyframe handle allocated");
  std::memcpy(mockLock(rightKey),mockLock(first),flatSize);
  auto* rk=(MockStopsData*)mockLock(rightKey);
  ensure(rk&&rk->magic==0x47524432u&&rk->count==2,
         "preexisting on-disk stop payload is binary compatible");
  // Pure selection/nextId metadata is safe to ignore.
  rk->selected=1;rk->nextId=9982;
  comparison=PF_ArbCompare_NOT_EQUAL;
  data={};data.which_function=PF_Arbitrary_COMPARE_FUNC;
  data.u.compare_func_params.a_arbH=first;
  data.u.compare_func_params.b_arbH=rightKey;
  data.u.compare_func_params.compareP=&comparison;
  ensure(perform(data)==PF_Err_NONE&&comparison==PF_ArbCompare_EQUAL,
         "selection and nextId metadata do not change keyframe values");
  // A stop ID changes intermediate matching when the number of stops
  // differs; AE must not mark keyframe values equal just because colors match.
  rk->items[0].id=9981;
  comparison=PF_ArbCompare_EQUAL;
  ensure(perform(data)==PF_Err_NONE&&comparison==PF_ArbCompare_NOT_EQUAL,
         "stable stop identity changes animated keyframe matching");
  rk->items[0].id=((MockStopsData*)mockLock(first))->items[0].id;
  // Insert a translucent red stop at t=.4 and keep the white/black endpoints.
  rk->items[2]=rk->items[1];
  rk->items[1]={.4f,1.f,.02f,.08f,.5f,3};
  rk->count=3;rk->selected=-1;rk->nextId=9983;
  MockStopsData frames[7]{};
  const double fractions[]={0.,.125,.499,.5,.501,.875,1.};
  for(int k=0;k<7;k++){
   PF_Handle interpolated=nullptr;
   data={};data.which_function=PF_Arbitrary_INTERP_FUNC;
   data.u.interp_func_params.left_arbH=first;
   data.u.interp_func_params.right_arbH=rightKey;
   data.u.interp_func_params.tF=fractions[k];
   data.u.interp_func_params.interpPH=&interpolated;
   ensure(perform(data)==PF_Err_NONE&&interpolated!=nullptr,
          "host-facing interpolate produces an owned arbitrary handle");
   ensure(mockSize(interpolated)==flatSize,
          "all interpolated frames preserve v1 arbitrary payload size");
   auto* value=(MockStopsData*)mockLock(interpolated);
   ensure(value&&value->magic==0x47524432u,"interpolated magic");
   ensure(value->count==(k==0?2:3),"2->3 count interpolates with ghosts");
   ensure(value->selected==-1,"interpolated selected UI stop is empty");
   for(int j=0;j<value->count;j++){
    ensure(std::isfinite(value->items[j].pos)&&
           std::isfinite(value->items[j].r)&&
           std::isfinite(value->items[j].a),
           "all interpolated positions/colors/alpha are finite");
    ensure(j==0||value->items[j-1].pos<=value->items[j].pos,
           "interpolated stop positions stay sorted");
   }
   frames[k]=*value;
   mockDispose(interpolated);
  }
  ensure(frames[0].items[0].pos==((MockStopsData*)mockLock(first))->items[0].pos,
         "left frame endpoint exact");
  ensure(frames[6].items[1].pos==.4f &&
         frames[6].items[1].a==.5f,
         "right frame added stop position and alpha exact");
  for(int j=0;j<3;j++){
   const auto& l=frames[2].items[j];
   const auto& r=frames[4].items[j];
   ensure(std::abs(l.pos-r.pos)<.01f&&
          std::abs(l.r-r.r)<.02f&&
          std::abs(l.a-r.a)<.02f,
          "no 50pct stop count jump in position, RGB or alpha");
  }
  // Reverse direction: the user can DELETE a keyed stop. This is the
  // actual AEX arbitrary callback, not just the header-only math test.
  MockStopsData reduced[7]{};
  for(int k=0;k<7;k++){
   PF_Handle interpolated=nullptr;
   data={};data.which_function=PF_Arbitrary_INTERP_FUNC;
   data.u.interp_func_params.left_arbH=rightKey;
   data.u.interp_func_params.right_arbH=first;
   data.u.interp_func_params.tF=fractions[k];
   data.u.interp_func_params.interpPH=&interpolated;
   ensure(perform(data)==PF_Err_NONE&&interpolated,
          "host reverse 3->2 interpolation returns allocated payload");
   auto* value=(MockStopsData*)mockLock(interpolated);
   ensure(value&&(value->count==(k==6?2:3)),
          "deleted stop collapses smoothly and endpoint restores 2 stops");
   reduced[k]=*value;
   mockDispose(interpolated);
  }
  for(int j=0;j<3;j++){
   const auto& l=reduced[2].items[j];
   const auto& r=reduced[4].items[j];
   ensure(std::abs(l.pos-r.pos)<.01f&&
          std::abs(l.r-r.r)<.02f&&
          std::abs(l.a-r.a)<.02f,
          "reverse stop deletion remains continuous at 50 pct");
  }
  for(PF_Handle h:{initial,first,fromEmpty,restored,rightKey}){
   data={};
   data.which_function=PF_Arbitrary_DISPOSE_FUNC;
   data.u.dispose_func_params.arbH=h;
   ensure(perform(data)==PF_Err_NONE,"arb disposal");
  }
  ensure(owned.empty(),"all host handles disposed");
 }
 // Failure injection verifies that partial parameter setup neither leaks
 // the default Gradient arbitrary handle nor registers an incomplete UI.
 for(int fail=1;fail<=14;fail++){
  params.clear();registered=0;addCount=0;injectedAddFailure=fail;
  PF_OutData pd{};
  const PF_Err err=effect(PF_Cmd_PARAMS_SETUP,&in,&pd,nullptr,nullptr,nullptr);
  ensure(err==PF_Err_OUT_OF_MEMORY,"injected PF_ADD_PARAM failure propagated");
  ensure(registered==0,"no UI registration after failed setup");
  for(const PF_ParamDef& p:params)if(p.param_type==PF_Param_ARBITRARY_DATA)
   mockDispose(p.u.arb_d.dephault);
  ensure(owned.empty(),"failed params setup leaves no live arb handle");
 }
 injectedAddFailure=-1;params.clear();addCount=0;
 PF_OutData pd{};
 ensure(effect(PF_Cmd_PARAMS_SETUP,&in,&pd,nullptr,nullptr,nullptr)==PF_Err_NONE,
        "setup recovers after injected failures");
 for(const auto& param:params)if(param.param_type==PF_Param_ARBITRARY_DATA)
  mockDispose(param.u.arb_d.dephault);
 ensure(owned.empty(),"post recovery cleanup");
 // Full SmartFX lifecycle simulation: parameter snapshot happens in PreRender;
 // SmartRender consumes immutable data WITHOUT calling checkout_param again.
 // We also compare the result to the legacy path while Start/End move.
 params.clear();registered=0;addCount=0;
 PF_OutData setup{};
 ensure(effect(PF_Cmd_PARAMS_SETUP,&in,&setup,nullptr,nullptr,nullptr)==PF_Err_NONE,
        "SmartFX test parameters registered");
 for(auto& param:params)if(param.param_type==PF_Param_ARBITRARY_DATA)
  param.u.arb_d.value=param.u.arb_d.dephault;
 in.inter.checkout_param=&fakeCheckoutParam;
 in.inter.checkin_param=&fakeCheckinParam;
 constexpr int w=64,h=48;
 in.width=w;in.height=h;in.current_time=0;in.time_step=1;in.time_scale=24;
 std::vector<uint8_t> source((size_t)w*h*4),smart(source.size()),legacy(source.size());
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  const size_t i=((size_t)y*w+x)*4;
  source[i+0]=(uint8_t)(255-(x%3)*64);
  source[i+1]=(uint8_t)(x*3);
  source[i+2]=(uint8_t)(y*5);
  source[i+3]=63;
 }
 fakeInputWorld=PF_EffectWorld{};
 fakeInputWorld.data=(PF_PixelPtr)source.data();
 fakeInputWorld.width=w;fakeInputWorld.height=h;fakeInputWorld.rowbytes=w*4;
 fakeOutputWorld=PF_EffectWorld{};
 fakeOutputWorld.data=(PF_PixelPtr)smart.data();
 fakeOutputWorld.width=w;fakeOutputWorld.height=h;fakeOutputWorld.rowbytes=w*4;
 PF_PreRenderCallbacks preCallbacks{};
 preCallbacks.checkout_layer=&fakeCheckoutLayer;
 PF_SmartRenderCallbacks renderCallbacks{};
 renderCallbacks.checkout_layer_pixels=&fakeCheckoutPixels;
 renderCallbacks.checkin_layer_pixels=&fakeCheckinPixels;
 renderCallbacks.checkout_output=&fakeCheckoutOutput;
 PF_PreRenderInput preInput{};
 preInput.bitdepth=8;
 preInput.output_request.rect={0,0,h,w};
 for(int frame=0;frame<8;frame++){
  PF_PreRenderOutput preOutput{};
  PF_PreRenderExtra preExtra{&preInput,&preOutput,&preCallbacks};
  in.current_time=frame;
  params[2].u.td.x_value+=(A_Fixed)(4000);
  params[3].u.td.y_value+=(A_Fixed)(3300);
  int previousCheckouts=checkedParamCount;
  int previousCheckins=checkedInParamCount;
  ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&preExtra)
         ==PF_Err_NONE,"Smart PreRender succeeds");
  ensure(preOutput.pre_render_data&&preOutput.delete_pre_render_data_func,
         "SmartFX snapshot allocated with host-owned cleanup");
  ensure(checkedParamCount-previousCheckouts==13,
         "PreRender checks out 13 render parameters");
  ensure(checkedInParamCount-previousCheckins==13,
         "all 13 render parameters checked in");
  PF_SmartRenderInput smartInput{};
  smartInput.bitdepth=8;
  smartInput.pre_render_data=preOutput.pre_render_data;
  PF_SmartRenderExtra renderExtra{&smartInput,&renderCallbacks};
  previousCheckouts=checkedParamCount;
  fakeOutputWorld.data=(PF_PixelPtr)smart.data();
  ensure(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&renderExtra)
         ==PF_Err_NONE,"SmartRender snapshot renders successfully");
  ensure(checkedParamCount==previousCheckouts,
         "SmartRender did not re-checkout parameters");
  PF_ParamDef inputDef{};
  inputDef.param_type=PF_Param_LAYER;
  inputDef.u.ld=fakeInputWorld;
  std::array<PF_ParamDef*,15> pp{};
  pp[0]=&inputDef;
  for(int i=1;i<15;i++)pp[i]=&params[(size_t)i-1];
  PF_LayerDef legacyOutput=fakeOutputWorld;
  legacyOutput.data=(PF_PixelPtr)legacy.data();
  ensure(effect(PF_Cmd_RENDER,&in,nullptr,pp.data(),&legacyOutput,nullptr)
         ==PF_Err_NONE,"legacy render for snapshot parity");
  ensure(std::memcmp(smart.data(),legacy.data(),smart.size())==0,
         "Smart snapshot pixels agree with regular render after point movement");
  preOutput.delete_pre_render_data_func(preOutput.pre_render_data);
 }
 // SmartFX result rect must follow upstream alpha bounds for Multiply:
 // this lets AE avoid full-comp redraw on alpha-masked/cropped layers.
 useNarrowInputROI=true;
 PF_PreRenderOutput roiOutput{};
 PF_PreRenderExtra roiExtra{&preInput,&roiOutput,&preCallbacks};
 ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&roiExtra)
        ==PF_Err_NONE,"input-bound PreRender");
 ensure(roiOutput.result_rect.left==12&&roiOutput.result_rect.top==10&&
        roiOutput.result_rect.right==30&&roiOutput.result_rect.bottom==28,
        "Alpha Multiply result_rect clipped to source opaque ROI");
 ensure(roiOutput.max_result_rect.left==8&&roiOutput.max_result_rect.top==6&&
        roiOutput.max_result_rect.right==34&&roiOutput.max_result_rect.bottom==32,
        "Alpha Multiply max_result_rect clipped to source bounds");
 ensure(roiOutput.solid==FALSE,
        "translucent input cannot be advertised solid to downstream effects");
 roiOutput.delete_pre_render_data_func(roiOutput.pre_render_data);
 // The same effect state must advertise IDENTICAL maximum support for
 // full-frame, tiny tiles, and empty (bounds-only) host requests.
 const auto originalRequest=preInput.output_request.rect;
 for(const PF_LRect requested:{PF_LRect{14,16,20,22},PF_LRect{0,0,0,0}}){
  preInput.output_request.rect=requested;
  PF_PreRenderOutput tiled{};
  PF_PreRenderExtra pre{&preInput,&tiled,&preCallbacks};
  ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&pre)
         ==PF_Err_NONE,"ROI tile pre-render");
  ensure(tiled.max_result_rect.left==8&&
         tiled.max_result_rect.top==6&&
         tiled.max_result_rect.right==34&&
         tiled.max_result_rect.bottom==32,
         "max_result_rect invariant under request ROI");
  const bool noRequestedPixels=requested.right==requested.left;
  ensure(noRequestedPixels?
         (tiled.result_rect.right==0&&tiled.result_rect.bottom==0):
         (tiled.result_rect.left==14&&tiled.result_rect.top==16&&
          tiled.result_rect.right==20&&tiled.result_rect.bottom==22),
         "result_rect follows intersected request ROI");
  tiled.delete_pre_render_data_func(tiled.pre_render_data);
 }
 preInput.output_request.rect=originalRequest;
 useNarrowInputROI=false;
 // Legal SmartFX case: PreRender without a subsequent SmartRender.
 PF_PreRenderOutput abandoned{};
 PF_PreRenderExtra incomplete{&preInput,&abandoned,&preCallbacks};
 ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&incomplete)
        ==PF_Err_NONE,"PreRender without Render succeeds");
 ensure(abandoned.pre_render_data&&abandoned.delete_pre_render_data_func,
        "abandoned PreRender snapshot has cleanup");
 abandoned.delete_pre_render_data_func(abandoned.pre_render_data);
 // Source-independent generator: Color Replace + Alpha Replace.
 // The upper layer must NOT trigger the prior AE effect stack.
 for(int colorPopup:{1,2}){ // Replace, Normal with fully opaque stop alpha
  params[10].u.pd.value=1; // Alpha Replace
  params[11].u.pd.value=colorPopup;
  const int requestsBefore=requestedLayerCount;
  const int pixelsBefore=checkedPixelsCount;
  PF_PreRenderOutput genOutput{};
  PF_PreRenderExtra genPre{&preInput,&genOutput,&preCallbacks};
  ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&genPre)
         ==PF_Err_NONE,"independent gradient PreRender");
  ensure(requestedLayerCount==requestsBefore,
         "independent generator does not request upstream render");
  ensure(genOutput.solid==TRUE,
         "opaque generator informs downstream effects of solid alpha");
  PF_SmartRenderInput genInput{};
  genInput.bitdepth=8;
  genInput.pre_render_data=genOutput.pre_render_data;
  PF_SmartRenderExtra genRender{&genInput,&renderCallbacks};
  fakeOutputWorld.data=(PF_PixelPtr)smart.data();
  ensure(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&genRender)
         ==PF_Err_NONE,"independent generator render");
  ensure(checkedPixelsCount==pixelsBefore,
         "independent generator never checks out input pixels");
  PF_ParamDef inputDef{};
  inputDef.param_type=PF_Param_LAYER;
  inputDef.u.ld=fakeInputWorld;
  std::array<PF_ParamDef*,15> pp{};
  pp[0]=&inputDef;
  for(int i=1;i<15;i++)pp[i]=&params[(size_t)i-1];
  PF_LayerDef legacyOutput=fakeOutputWorld;
  legacyOutput.data=(PF_PixelPtr)legacy.data();
  ensure(effect(PF_Cmd_RENDER,&in,nullptr,pp.data(),&legacyOutput,nullptr)
         ==PF_Err_NONE,"independent generator legacy");
  ensure(std::memcmp(smart.data(),legacy.data(),smart.size())==0,
         "generator SmartFX pixels match legacy");
  genOutput.delete_pre_render_data_func(genOutput.pre_render_data);
 }
 // Color Normal + Alpha Normal over fully opaque stops must be fully
 // source-independent even on transparent layers, so the host must NOT
 // render any upstream effect tiles for this instance.
 for(int colorPopup:{1,2}){
  params[10].u.pd.value=2; // Alpha Normal
  params[11].u.pd.value=colorPopup;
  const int beforeRequests=requestedLayerCount;
  const int beforePixels=checkedPixelsCount.load();
  PF_PreRenderOutput generated{};
  PF_PreRenderExtra pre{&preInput,&generated,&preCallbacks};
  ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&pre)
         ==PF_Err_NONE,"opaque Normal alpha PreRender");
  ensure(requestedLayerCount==beforeRequests,
         "opaque Normal alpha does not request preceding effect stack");
  ensure(generated.solid==TRUE,
         "opaque Normal alpha exposes correct solid output hint");
  PF_SmartRenderInput input{};
  input.bitdepth=8;input.pre_render_data=generated.pre_render_data;
  PF_SmartRenderExtra smartExtra{&input,&renderCallbacks};
  fakeOutputWorld.data=(PF_PixelPtr)smart.data();
  ensure(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&smartExtra)
         ==PF_Err_NONE,"opaque Normal alpha generator render");
  ensure(checkedPixelsCount.load()==beforePixels,
         "opaque Normal alpha does not checkout upstream pixels");
  PF_ParamDef inDef{};inDef.param_type=PF_Param_LAYER;inDef.u.ld=fakeInputWorld;
  std::array<PF_ParamDef*,15> pointers{};pointers[0]=&inDef;
  for(int i=1;i<15;i++)pointers[i]=&params[(size_t)i-1];
  PF_LayerDef legacyOutput=fakeOutputWorld;
  legacyOutput.data=(PF_PixelPtr)legacy.data();
  ensure(effect(PF_Cmd_RENDER,&in,nullptr,pointers.data(),&legacyOutput,nullptr)
         ==PF_Err_NONE,"opaque Normal alpha legacy output");
  ensure(std::memcmp(smart.data(),legacy.data(),smart.size())==0,
         "opaque Normal alpha SmartFX and legacy exact parity");
  generated.delete_pre_render_data_func(generated.pre_render_data);
 }
 // Uniform sRGB gradient is spatially constant even when the user
 // selects Mapping. No luminance/source checkout is needed when both
 // color and alpha blend are Replace (or normalized fully opaque Normal).
 auto* currentStops=(MockStopsData*)mockLock(params[9].u.arb_d.value);
 ensure(currentStops&&currentStops->count==2,
        "uniform gradient mock source initialized");
 const MockStopData savedSecond=currentStops->items[1];
 currentStops->items[1].r=currentStops->items[0].r;
 currentStops->items[1].g=currentStops->items[0].g;
 currentStops->items[1].b=currentStops->items[0].b;
 currentStops->items[1].a=currentStops->items[0].a;
 params[0].u.pd.value=4; // Mapping
 params[10].u.pd.value=2; // Alpha Normal, constant alpha=1
 params[11].u.pd.value=2; // Color Normal
 {
  const int requestsBefore=requestedLayerCount;
  const int pixelsBefore=checkedPixelsCount.load();
  PF_PreRenderOutput prepared{};
  PF_PreRenderExtra pre{&preInput,&prepared,&preCallbacks};
  ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&pre)
         ==PF_Err_NONE,"uniform Mapping PreRender");
  ensure(requestedLayerCount==requestsBefore,
         "uniform Mapping bypasses all earlier effects");
  ensure(prepared.solid==TRUE,
         "uniform Mapping full-alpha output solid");
  PF_SmartRenderInput renderInput{};
  renderInput.bitdepth=8;
  renderInput.pre_render_data=prepared.pre_render_data;
  PF_SmartRenderExtra renderInfo{&renderInput,&renderCallbacks};
  fakeOutputWorld.data=(PF_PixelPtr)smart.data();
  ensure(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&renderInfo)
         ==PF_Err_NONE,"uniform Mapping output rendered");
  ensure(checkedPixelsCount.load()==pixelsBefore,
         "uniform Mapping skips source pixel checkout");
  PF_ParamDef inputDef{};inputDef.param_type=PF_Param_LAYER;
  inputDef.u.ld=fakeInputWorld;
  std::array<PF_ParamDef*,15> p{};
  p[0]=&inputDef;
  for(int j=1;j<15;j++)p[j]=&params[(size_t)j-1];
  PF_LayerDef expected=fakeOutputWorld;
  expected.data=(PF_PixelPtr)legacy.data();
  ensure(effect(PF_Cmd_RENDER,&in,nullptr,p.data(),&expected,nullptr)
         ==PF_Err_NONE,"uniform Mapping legacy effect");
  ensure(std::memcmp(smart.data(),legacy.data(),smart.size())==0,
         "uniform Mapping SmartFX and legacy output agree");
  prepared.delete_pre_render_data_func(prepared.pre_render_data);
 }
 currentStops->items[1]=savedSecond;
 params[0].u.pd.value=1; // restore Linear for subsequent test fixtures
 // Zero-alpha stops: Alpha Replace or Multiply makes premultiplied output
 // fully transparent for ALL color blends and gradient shapes. Verify real
 // AEX callbacks avoid upstream requests and LUT creation for all depths.
 const float savedAlpha0=currentStops->items[0].a;
 const float savedAlpha1=currentStops->items[1].a;
 currentStops->items[0].a=0.f;currentStops->items[1].a=0.f;
 for(int typePopup:{1,4})for(int alphaPopup:{1,6})
 for(int colorPopup:{1,2,6,33}){
  params[0].u.pd.value=typePopup;
  params[10].u.pd.value=alphaPopup;
  params[11].u.pd.value=colorPopup;
  const int requestsBefore=requestedLayerCount;
  PF_PreRenderOutput transparent{};
  PF_PreRenderExtra pre{&preInput,&transparent,&preCallbacks};
  ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&pre)
         ==PF_Err_NONE,"zero-alpha AE pre-render");
  ensure(requestedLayerCount==requestsBefore,
         "zero-alpha all-shape all-color no upstream checkout");
  ensure(transparent.solid==FALSE,
         "zero-alpha gradient never advertises opaque AE output");
  ensure(transparent.result_rect.left==0&&transparent.result_rect.top==0&&
         transparent.result_rect.right==0&&transparent.result_rect.bottom==0&&
         transparent.max_result_rect.left==0&&transparent.max_result_rect.top==0&&
         transparent.max_result_rect.right==0&&transparent.max_result_rect.bottom==0,
         "transparent effect advertises empty pixel bounds to avoid wasted AE work");
  for(int depth:{8,16,32}){
   const int bpp=depth==8?4:depth==16?8:16;
   std::vector<uint8_t> pixels((size_t)w*h*bpp,0xf4);
   fakeOutputWorld.data=(PF_PixelPtr)pixels.data();
   fakeOutputWorld.rowbytes=w*bpp;
   PF_SmartRenderInput input{};
   input.bitdepth=(short)depth;
   input.pre_render_data=transparent.pre_render_data;
   PF_SmartRenderExtra smartExtra{&input,&renderCallbacks};
   const int checkedBefore=checkedPixelsCount.load();
   ensure(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&smartExtra)
          ==PF_Err_NONE,"zero-alpha SmartRender depth");
   ensure(checkedPixelsCount.load()==checkedBefore,
          "zero-alpha SmartRender never retrieves prior effects");
   ensure(std::all_of(pixels.begin(),pixels.end(),
                      [](uint8_t c){return c==0;}),
          "zero-alpha SmartRender all channels exactly zero");
  }
  transparent.delete_pre_render_data_func(transparent.pre_render_data);
 }
 currentStops->items[0].a=savedAlpha0;
 currentStops->items[1].a=savedAlpha1;
 fakeOutputWorld.data=(PF_PixelPtr)smart.data();
 fakeOutputWorld.rowbytes=w*4;
 params[0].u.pd.value=1;
 params[10].u.pd.value=2;
 params[11].u.pd.value=2;
 // AE reports an opaque source (solid layer or a fully opaque prior
 // effect). Common default Color Normal/Alpha Multiply should not force
 // checkout_layer_pixels. The output must match ordinary rendering.
 params[10].u.pd.value=6; // Alpha Multiply
 params[11].u.pd.value=2; // Color Normal
 mockOpaqueSource=true;
 for(int i=0;i<w*h;i++)source[(size_t)i*4]=255;
 const int pixelsBeforeSolid=checkedPixelsCount;
 const int layerRequestsBefore=requestedLayerCount;
 PF_PreRenderOutput solidOutput{};
 PF_PreRenderExtra solidPre{&preInput,&solidOutput,&preCallbacks};
 ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&solidPre)
        ==PF_Err_NONE,"host-solid upstream metadata");
 ensure(requestedLayerCount==layerRequestsBefore+1,
        "solid property discovered through normal upstream checkout");
 ensure(solidOutput.solid==TRUE,
        "host-solid upstream produces solid output hint");
 PF_SmartRenderInput solidInput{};
 solidInput.bitdepth=8;solidInput.pre_render_data=solidOutput.pre_render_data;
 PF_SmartRenderExtra solidRender{&solidInput,&renderCallbacks};
 fakeOutputWorld.data=(PF_PixelPtr)smart.data();
 ensure(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&solidRender)
        ==PF_Err_NONE,"host-solid generator render");
 ensure(checkedPixelsCount==pixelsBeforeSolid,
        "host-solid source avoids expensive upstream pixel checkout");
 {
  PF_ParamDef inputDef{};
  inputDef.param_type=PF_Param_LAYER;inputDef.u.ld=fakeInputWorld;
  std::array<PF_ParamDef*,15> pp{};pp[0]=&inputDef;
  for(int k=1;k<15;k++)pp[k]=&params[(size_t)k-1];
  PF_LayerDef legacyOutput=fakeOutputWorld;
  legacyOutput.data=(PF_PixelPtr)legacy.data();
  ensure(effect(PF_Cmd_RENDER,&in,nullptr,pp.data(),&legacyOutput,nullptr)
         ==PF_Err_NONE,"host-solid reference legacy render");
  ensure(std::memcmp(smart.data(),legacy.data(),legacy.size())==0,
         "host-solid fast path matches opaque-input legacy render");
 }
 solidOutput.delete_pre_render_data_func(solidOutput.pre_render_data);
 mockOpaqueSource=false;
 // Multiframe rendering host simulation. AE may render 8 effect instances
 // concurrently on different worker threads. Snapshot state and the four-way
 // LUT cache must have no cross-thread mutable memory.
 params[10].u.pd.value=6; // Alpha Multiply
 params[11].u.pd.value=2; // Color Normal
 constexpr int nWorkers=8;
 std::array<PF_PreRenderOutput,nWorkers> snapshots{};
 std::array<std::vector<uint8_t>,nWorkers> concurrentFrames{};
 std::array<A_Fixed,nWorkers> xPositions{},yPositions{};
 for(int i=0;i<nWorkers;i++){
  params[2].u.td.x_value+=(A_Fixed)2222;
  params[3].u.td.y_value+=(A_Fixed)1544;
  xPositions[i]=params[2].u.td.x_value;
  yPositions[i]=params[3].u.td.y_value;
  PF_PreRenderExtra extra{&preInput,&snapshots[i],&preCallbacks};
  ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&extra)
         ==PF_Err_NONE,"MFR PreRender evaluation");
  ensure(snapshots[i].pre_render_data,"MFR owns its immutable snapshot");
  concurrentFrames[i].resize((size_t)w*h*4);
 }
 std::atomic<int> mfrErrors{0};
 const int beforeMfrChecks=checkedParamCount;
 std::array<std::thread,nWorkers> workers;
 for(int i=0;i<nWorkers;i++){
  workers[i]=std::thread([&,i](){
   PF_EffectWorld output=fakeOutputWorld;
   output.data=(PF_PixelPtr)concurrentFrames[i].data();
   threadOutput=&output;
   PF_SmartRenderInput input{};
   input.bitdepth=8;
   input.pre_render_data=snapshots[i].pre_render_data;
   PF_SmartRenderExtra extra{&input,&renderCallbacks};
   if(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&extra)!=PF_Err_NONE)
    mfrErrors++;
   threadOutput=nullptr;
  });
 }
 for(auto& t:workers)t.join();
 ensure(mfrErrors.load()==0,"eight MFR workers without errors");
 ensure(checkedParamCount==beforeMfrChecks,
        "MFR worker render never checks out mutable AE parameters");
 for(int i=0;i<nWorkers;i++){
  params[2].u.td.x_value=xPositions[i];
  params[3].u.td.y_value=yPositions[i];
  PF_ParamDef inputDef{};
  inputDef.param_type=PF_Param_LAYER;
  inputDef.u.ld=fakeInputWorld;
  std::array<PF_ParamDef*,15> pp{};
  pp[0]=&inputDef;
  for(int k=1;k<15;k++)pp[k]=&params[(size_t)k-1];
  PF_LayerDef outWorld=fakeOutputWorld;
  outWorld.data=(PF_PixelPtr)legacy.data();
  ensure(effect(PF_Cmd_RENDER,&in,nullptr,pp.data(),&outWorld,nullptr)==PF_Err_NONE,
         "baseline render for MFR output");
  ensure(std::memcmp(legacy.data(),concurrentFrames[i].data(),legacy.size())==0,
         "parallel MFR renderer matches serialized exact output");
  snapshots[i].delete_pre_render_data_func(snapshots[i].pre_render_data);
 }
 // Full HD 32-bpc AEX SmartFX integration benchmark, with real DLL
 // callbacks and mock SDK host. Unlike a raster-only benchmark, timing
 // includes the 12 parameter checkouts, pre-render snapshot, layer metadata,
 // cache access, output world checkout, and actual 33MB output write.
 // Still does NOT measure AE's own scheduling, color management, or viewers.
 {
  constexpr int B_WIDTH=1920,B_HEIGHT=1080;
  std::vector<float> f32Source((size_t)B_WIDTH*B_HEIGHT*4);
  std::vector<float> f32Output(f32Source.size());
  for(int y=0;y<B_HEIGHT;y++)for(int x=0;x<B_WIDTH;x++){
   const size_t at=((size_t)y*B_WIDTH+(size_t)x)*4;
   const float a=((x+y)%13==0)?0.44f:1.f;
   f32Source[at+0]=a;
   f32Source[at+1]=a*.37f;
   f32Source[at+2]=a*.72f;
   f32Source[at+3]=a*.18f;
  }
  fakeInputWorld.width=B_WIDTH;fakeInputWorld.height=B_HEIGHT;
  fakeInputWorld.rowbytes=B_WIDTH*16;
  fakeInputWorld.data=(PF_PixelPtr)f32Source.data();
  fakeOutputWorld.width=B_WIDTH;fakeOutputWorld.height=B_HEIGHT;
  fakeOutputWorld.rowbytes=B_WIDTH*16;
  fakeOutputWorld.data=(PF_PixelPtr)f32Output.data();
  in.width=B_WIDTH;in.height=B_HEIGHT;
  preInput.output_request.rect={0,0,B_HEIGHT,B_WIDTH};
  params[0].u.pd.value=1; // Linear
  params[10].u.pd.value=6; // Alpha Multiply (factory default)
  params[11].u.pd.value=2; // Color Normal (factory default)
  const auto *stopData=(const MockStopsData*)mockLock(params[9].u.arb_d.value);
  ensure(stopData&&stopData->count==2&&
         stopData->items[0].a==1.f&&stopData->items[1].a==1.f,
         "host full-HD benchmark uses initial opaque two-stop gradient");
  for(int scenario=0;scenario<3;scenario++){
   // Scenario 0: transparent pixels, AE source dependency mandatory.
   // Scenario 1: AE upstream metadata marks source fully opaque.
   // Scenario 2: Alpha Replace makes source pixels fully irrelevant.
   mockOpaqueSource=scenario==1;
   params[10].u.pd.value=scenario==2?1:6;
   std::vector<double> preTimes,renderTimes,totalTimes;
   for(int rep=0;rep<7;rep++){
    PF_PreRenderOutput snapshot{};
    PF_PreRenderExtra pre{&preInput,&snapshot,&preCallbacks};
    const int previousChecks=checkedPixelsCount.load();
    const auto before=std::chrono::steady_clock::now();
    ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&pre)
           ==PF_Err_NONE,"full HD 32bpc AEX Smart PreRender");
    const auto middle=std::chrono::steady_clock::now();
    ensure(snapshot.pre_render_data&&snapshot.delete_pre_render_data_func,
           "full HD 32bpc frame snapshot prepared");
    PF_SmartRenderInput input{};
    input.bitdepth=32;
    input.pre_render_data=snapshot.pre_render_data;
    PF_SmartRenderExtra extra{&input,&renderCallbacks};
    ensure(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&extra)
           ==PF_Err_NONE,"full HD 32bpc AEX Smart Render");
    const auto after=std::chrono::steady_clock::now();
    snapshot.delete_pre_render_data_func(snapshot.pre_render_data);
    if(scenario!=0)
     ensure(checkedPixelsCount.load()==previousChecks,
            "fully opaque or independent full HD skips pixel checkout");
    preTimes.push_back(std::chrono::duration<double,std::milli>(
                        middle-before).count());
    renderTimes.push_back(std::chrono::duration<double,std::milli>(
                        after-middle).count());
    totalTimes.push_back(std::chrono::duration<double,std::milli>(
                        after-before).count());
   }
   std::sort(preTimes.begin(),preTimes.end());
   std::sort(renderTimes.begin(),renderTimes.end());
   std::sort(totalTimes.begin(),totalTimes.end());
   std::cout<<"MOCK AE FULL HD 32bpc "
    <<(scenario==0?"partial alpha":scenario==1?
       "upstream metadata solid":"source-independent alpha Replace")
    <<" median_pre_ms="<<std::fixed<<std::setprecision(3)<<preTimes[3]
    <<" median_smart_render_ms="<<renderTimes[3]
    <<" median_callbacks_total_ms="<<totalTimes[3]
    <<" source_pixel_checkouts_delta="
    <<(scenario==0?">0":"0")<<"\\n";
  }
  mockOpaqueSource=false;
 }

 // Optional real CUDA + mock Adobe host suite. GPU memory is genuine,
 // every shader launch is through the compiled AEX EffectMain callback.
 if(std::getenv("GRADATION_TEST_CUDA")){
  ensure(cuInit(0)==CUDA_SUCCESS,"CUDA driver init");
  CUdevice dev{};ensure(cuDeviceGet(&dev,0)==CUDA_SUCCESS,"CUDA device");
  ensure(cuDevicePrimaryCtxRetain(&mockgpu::context,dev)==CUDA_SUCCESS,"CUDA context retain");
  ensure(cuCtxPushCurrent(mockgpu::context)==CUDA_SUCCESS,"CUDA context push");
  ensure(cuStreamCreate(&mockgpu::stream,CU_STREAM_NON_BLOCKING)==CUDA_SUCCESS,"CUDA stream");
  SPBasicSuite basic{};mockgpu::initSuites(basic);in.pica_basicP=&basic;
  PF_GPUDeviceSetupInput gi{};gi.what_gpu=PF_GPU_Framework_CUDA;
  PF_GPUDeviceSetupOutput go{};PF_GPUDeviceSetupExtra ge{&gi,&go};PF_OutData flags{};
  ensure(effect(PF_Cmd_GPU_DEVICE_SETUP,&in,&flags,nullptr,nullptr,&ge)==PF_Err_NONE&&go.gpu_data,
         "AEX real CUDA device setup");
  ensure(flags.out_flags2&PF_OutFlag2_SUPPORTS_GPU_RENDER_F32,"device GPU support advertised");
  constexpr int gw=1920,gh=1080;
  const size_t n=size_t(gw)*gh*4,nb=n*sizeof(float);
  CUdeviceptr src=0,dst=0;
  ensure(cuMemAlloc(&src,nb)==CUDA_SUCCESS&&cuMemAlloc(&dst,nb)==CUDA_SUCCESS,"CUDA image memory");
  std::vector<float> argb(n),bgra(n),cpu(n),gpu(n);
  for(size_t i=0;i<n;i+=4){
   const float a=(i/4%13==0)?.44f:1.f;
   argb[i]=a;argb[i+1]=a*.37f;argb[i+2]=a*.72f;argb[i+3]=a*.18f;
   bgra[i]=argb[i+3];bgra[i+1]=argb[i+2];bgra[i+2]=argb[i+1];bgra[i+3]=a;
  }
  ensure(cuMemcpyHtoD(src,bgra.data(),nb)==CUDA_SUCCESS,"test upload");
  fakeInputWorld={};fakeOutputWorld={};
  fakeInputWorld.width=fakeOutputWorld.width=gw;fakeInputWorld.height=fakeOutputWorld.height=gh;
  fakeInputWorld.rowbytes=fakeOutputWorld.rowbytes=gw*16;
  in.width=gw;in.height=gh;preInput.bitdepth=32;
  preInput.output_request.rect={0,0,gh,gw};
  preInput.what_gpu=PF_GPU_Framework_CUDA;preInput.gpu_data=go.gpu_data;
  params[2].u.td.x_value=0;params[2].u.td.y_value=0;
  params[3].u.td.x_value=gw*65536;params[3].u.td.y_value=0;
  auto* stops=(MockStopsData*)mockLock(params[9].u.arb_d.value);
  stops->items[0].r=.13f;stops->items[0].g=.27f;stops->items[0].b=.76f;
  stops->items[1].r=.91f;stops->items[1].g=.65f;stops->items[1].b=.12f;
  stops->items[0].a=.35f;stops->items[1].a=.95f;
  ensure(params[13].uu.id==14&&params[13].u.pd.num_choices==3,"Render Device preserves established parameter IDs");
  for(int choice=1;choice<=3;choice++){
   params[13].u.pd.value=choice;
   for(int depth:{8,16,32}){
    preInput.bitdepth=(short)depth;
    PF_PreRenderOutput test{};PF_PreRenderExtra pe{&preInput,&test,&preCallbacks};
    ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&pe)==PF_Err_NONE,"device selection pre-render");
    ensure(bool(test.flags&PF_RenderOutputFlag_GPU_RENDER_POSSIBLE)==(depth==32&&choice!=2),"CPU disables GPU; Auto/GPU permit host-supported CUDA only");
    test.delete_pre_render_data_func(test.pre_render_data);
   }
  }
  preInput.bitdepth=32;params[13].u.pd.value=1;
  std::cout<<"PASS Render Device Auto/CPU/GPU across 8/16/32bpc\n";
  std::vector<double> times;double maximumError=0;
  const int modes[][2]={{1,1},{1,6},{2,6},{13,6},{30,2}};
  for(int shape=1;shape<=4;shape++){
   params[0].u.pd.value=shape;
   for(auto& pair:modes){
    params[11].u.pd.value=pair[0];params[10].u.pd.value=pair[1];
    if(shape==4&&pair[0]==1&&pair[1]==1)continue;
    PF_PreRenderOutput snap{};PF_PreRenderExtra pre{&preInput,&snap,&preCallbacks};
    ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&pre)==PF_Err_NONE,
          "GPU PreRender");
    ensure(snap.flags&PF_RenderOutputFlag_GPU_RENDER_POSSIBLE,"GPU pre-render flag");
    PF_SmartRenderInput si{};si.bitdepth=32;si.pre_render_data=snap.pre_render_data;
    si.what_gpu=PF_GPU_Framework_CUDA;si.gpu_data=go.gpu_data;
    PF_SmartRenderExtra sr{&si,&renderCallbacks};
    fakeInputWorld.data=(PF_PixelPtr)argb.data();fakeOutputWorld.data=(PF_PixelPtr)cpu.data();
    ensure(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&sr)==PF_Err_NONE,"CPU reference");
    fakeInputWorld.data=(PF_PixelPtr)src;fakeOutputWorld.data=(PF_PixelPtr)dst;
    ensure(effect(PF_Cmd_SMART_RENDER_GPU,&in,nullptr,nullptr,nullptr,&sr)==PF_Err_NONE,"AEX GPU render warmup");
    std::vector<double> local;
    for(int repeat=0;repeat<12;repeat++){
     auto start=std::chrono::steady_clock::now();
     ensure(effect(PF_Cmd_SMART_RENDER_GPU,&in,nullptr,nullptr,nullptr,&sr)==PF_Err_NONE,"AEX GPU render");
     local.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }
    std::sort(local.begin(),local.end());times.push_back(local[local.size()/2]);
    ensure(cuMemcpyDtoH(gpu.data(),dst,nb)==CUDA_SUCCESS,"test download");
    double error=0;
    for(size_t i=0;i<n;i+=4){
     for(int j=0;j<4;j++)error=std::max(error,double(std::abs(cpu[i+j]-gpu[i+3-j])));
    }
    maximumError=std::max(maximumError,error);
    std::cout<<"GPU AEX shape "<<shape<<" color "<<pair[0]<<" alpha "<<pair[1]
             <<" median "<<local[local.size()/2]<<"ms maxAbs "<<error<<"\n";
    ensure(error<.002,"CPU/GPU numeric parity");
    snap.delete_pre_render_data_func(snap.pre_render_data);
   }
  }
  double avg=0;for(double t:times)avg+=t;avg/=times.size();
  std::cout<<"GPU AEX 1080p "<<times.size()<<" cases mean "<<avg<<"ms maxAbs "<<maximumError<<"\n";

  // Full mode/space verification on offset, padded ROI, using identical
  // snapshots on CPU and the real AEX GPU command.
  constexpr int tw=127,th=73;
  fakeInputWorld.width=tw;fakeInputWorld.height=th;
  fakeInputWorld.origin_x=5;fakeInputWorld.origin_y=3;
  fakeOutputWorld.width=tw;fakeOutputWorld.height=th;
  fakeOutputWorld.origin_x=8;fakeOutputWorld.origin_y=7;
  in.width=tw;in.height=th;
  preInput.output_request.rect={7,8,7+th,8+tw};
  params[2].u.td.x_value=16*65536;params[2].u.td.y_value=9*65536;
  params[3].u.td.x_value=112*65536;params[3].u.td.y_value=55*65536;
  int count=0;double suiteError=0;
  auto compare=[&](int shape,int color,int alpha,int space){
   params[0].u.pd.value=shape;params[11].u.pd.value=color;
   params[10].u.pd.value=alpha;params[8].u.pd.value=space;
   PF_PreRenderOutput snap{};PF_PreRenderExtra pre{&preInput,&snap,&preCallbacks};
   ensure(effect(PF_Cmd_SMART_PRE_RENDER,&in,nullptr,nullptr,nullptr,&pre)==PF_Err_NONE,"GPU parity PreRender");
   PF_SmartRenderInput si{};si.bitdepth=32;si.pre_render_data=snap.pre_render_data;
   si.what_gpu=PF_GPU_Framework_CUDA;si.gpu_data=go.gpu_data;
   PF_SmartRenderExtra sr{&si,&renderCallbacks};
   fakeInputWorld.data=(PF_PixelPtr)argb.data();fakeOutputWorld.data=(PF_PixelPtr)cpu.data();
   ensure(effect(PF_Cmd_SMART_RENDER,&in,nullptr,nullptr,nullptr,&sr)==PF_Err_NONE,"GPU parity CPU ref");
   fakeInputWorld.data=(PF_PixelPtr)src;fakeOutputWorld.data=(PF_PixelPtr)dst;
   ensure(effect(PF_Cmd_SMART_RENDER_GPU,&in,nullptr,nullptr,nullptr,&sr)==PF_Err_NONE,"GPU parity AEX callback");
   CUDA_MEMCPY2D cp{};cp.srcMemoryType=CU_MEMORYTYPE_DEVICE;cp.srcDevice=dst;cp.srcPitch=gw*16;
   cp.dstMemoryType=CU_MEMORYTYPE_HOST;cp.dstHost=gpu.data();cp.dstPitch=gw*16;
   cp.WidthInBytes=tw*16;cp.Height=th;
   ensure(cuMemcpy2D(&cp)==CUDA_SUCCESS,"GPU parity ROI download");
   double err=0;
   for(int y=0;y<th;y++)for(int x=0;x<tw;x++){
    size_t i=(size_t(y)*gw+x)*4;
    for(int j=0;j<4;j++){
     if(!std::isfinite(cpu[i+j])||!std::isfinite(gpu[i+3-j]))err=1e30;
     else err=std::max(err,double(std::abs(cpu[i+j]-gpu[i+3-j])));
    }
   }
   if(err>=.002)std::cout<<"PARITY FAIL shape="<<shape<<" color="<<color<<" alpha="<<alpha<<" space="<<space<<" err="<<err<<"\n";
   ensure(err<.002,"CPU/GPU expanded mode and ROI parity");
   suiteError=std::max(suiteError,err);++count;
   snap.delete_pre_render_data_func(snap.pre_render_data);
  };
  for(int shape=1;shape<=4;shape++){
   for(int color=1;color<=33;color++)compare(shape,color,6,1);
   for(int alpha=1;alpha<=29;alpha++)compare(shape,2,alpha,1);
  }
  // Non-affine LUTs (all actual popup choices, excluding separators).
  for(int space:{1,2,4,5,6,8,9,11,12,13,15,16,18,19})
   for(int shape=1;shape<=4;shape++)compare(shape,2,6,space);
  std::cout<<"GPU AEX extended parity "<<count<<" cases maxAbs "
           <<std::setprecision(9)<<suiteError<<"\n";
  PF_GPUDeviceSetdownInput down{};down.gpu_data=go.gpu_data;down.what_gpu=PF_GPU_Framework_CUDA;
  PF_GPUDeviceSetdownExtra de{&down};
  ensure(effect(PF_Cmd_GPU_DEVICE_SETDOWN,&in,nullptr,nullptr,nullptr,&de)==PF_Err_NONE,"GPU device teardown");
  ensure(mockgpu::allocations==0,"GPU plugin allocations fully released");
  cuMemFree(src);cuMemFree(dst);cuStreamDestroy(mockgpu::stream);
  CUcontext popped{};cuCtxPopCurrent(&popped);cuDevicePrimaryCtxRelease(dev);
  in.pica_basicP=nullptr;
 }
 ensure(checkedPixelsCount==checkedInPixelsCount,
        "input pixel checkouts/returns balanced across render calls");
 for(const auto& param:params)if(param.param_type==PF_Param_ARBITRARY_DATA)
  mockDispose(param.u.arb_d.dephault);
 ensure(owned.empty(),"SmartFX snapshot test leaves no AE handle leaks");
 // Optional idle observation with the exact loaded AEX, after actual
 // CPU and (if enabled) GPU callbacks have finished. No GUI or AE launch.
 if(GetEnvironmentVariableW(L"GRADATION_TEST_IDLE",nullptr,0)){
  FILETIME c{},e{},k0{},u0{},k1{},u1{};
  const auto ticks=[](FILETIME t){ULARGE_INTEGER v{};v.LowPart=t.dwLowDateTime;v.HighPart=t.dwHighDateTime;return v.QuadPart;};
  std::cout<<"IDLE_BEGIN pid="<<GetCurrentProcessId()<<std::endl;
  GetProcessTimes(GetCurrentProcess(),&c,&e,&k0,&u0);
  const auto start=std::chrono::steady_clock::now();
  Sleep(8000);
  GetProcessTimes(GetCurrentProcess(),&c,&e,&k1,&u1);
  const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  SYSTEM_INFO info{};GetSystemInfo(&info);
  const double cpu=double(ticks(k1)-ticks(k0)+ticks(u1)-ticks(u0))*1e-7;
  std::cout<<"IDLE_END seconds="<<elapsed<<" CPU_core_ms="<<cpu*1000
    <<" CPU_system_pct="<<100*cpu/elapsed/std::max<DWORD>(1,info.dwNumberOfProcessors)<<std::endl;
 }
 FreeLibrary(lib);
 std::cout<<"PASS startup host simulation: 25x GLOBAL/PARAMS setup, UI, 50 arbitrary copies, flatten/unflatten/compare/dispose\n";
 return 0;
}
