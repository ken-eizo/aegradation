// Gradation SlugUI RGB/Alpha editor.
// Host integration: Adobe Effect SDK PF_Cmd_EVENT on the UI thread.
// Window lifetime: Windows DialogBoxIndirectParamW / EndDialog, NOT
// CreateWindowEx + EnableWindow + a home-made GetMessage loop.
// GPU objects are scoped to this dialog and released only AFTER
// DialogBoxIndirectParamW returns (not during WM_DESTROY).
// Native owner enable/disable and keyboard modal routing are handled
// automatically by Windows' documented dialog manager.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <slugia/win32_surface.hpp>
#include <slugia/vulkan_renderer.hpp>
#include <slugia/vulkan_interop.hpp>
#include <slugia/vector_atlas.hpp>
#include <slugia/input.hpp>
#include <slugia/ui.hpp>
#include <slugia/draw_list.hpp>
#include "picker_ui.hpp"
#include "picker_text_metrics.hpp"
#include "picker_win32_keys.hpp"
#include "picker_renderer_config.hpp"
#include "picker_screen_scale.hpp"
#include "picker_bootstrap_gdi.hpp"
#include "figma_scene.hpp"
#include "picker_draw_submission.hpp"
#include "../stop_rgba_dialog_embedded.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace su=slugia;
namespace gp=gradation::picker;
namespace {
constexpr wchar_t kDialogTitle[]=L"Color";
constexpr int WIDTH=797,HEIGHT=444;
// Allow Windows to show/activate its modal before any font or Vulkan work.
constexpr UINT WM_START_PICKER=WM_APP+0x249;
std::string readClipboardUtf8(HWND owner){
 if(!OpenClipboard(owner))return {};
 struct ClipboardClose { ~ClipboardClose(){CloseClipboard();} } close;
 auto handle=GetClipboardData(CF_UNICODETEXT);
 std::string result;
 if(handle){
  auto* wide=(const wchar_t*)GlobalLock(handle);
  if(wide){
   struct Unlock { HGLOBAL handle; ~Unlock(){GlobalUnlock(handle);} } unlock{handle};
   const int bytes=WideCharToMultiByte(CP_UTF8,0,wide,-1,nullptr,0,nullptr,nullptr);
   if(bytes>1){
    result.resize(size_t(bytes));
    WideCharToMultiByte(CP_UTF8,0,wide,-1,result.data(),bytes,nullptr,nullptr);
    result.pop_back();
   }
  }
 }
 return result;
}
void writeClipboardUtf8(HWND owner,std::string_view text){
 const int wlen=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
                                    text.data(),int(text.size()),nullptr,0);
 if(wlen<=0&&!text.empty())return;
 HGLOBAL handle=GlobalAlloc(GMEM_MOVEABLE,(size_t(wlen)+1)*sizeof(wchar_t));
 if(!handle)return;
 auto* wide=(wchar_t*)GlobalLock(handle);
 if(!wide){GlobalFree(handle);return;}
 if(wlen>0)MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),wide,wlen);
 wide[wlen]=0;
 GlobalUnlock(handle);
 if(OpenClipboard(owner)){
  if(EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,handle))handle=nullptr;
  CloseClipboard();
 }
 if(handle)GlobalFree(handle);
}
// Self-contained cursor resource: no private Adobe resource or OS modification.
double timeNow() noexcept {
 using C=std::chrono::steady_clock;
 return std::chrono::duration<double>(C::now().time_since_epoch()).count();
}
su::UiSkin makeSkin(){
 su::UiSkin skin{};
 skin.text.fontName="Source Sans Pro";
 skin.text.size=12.f;
 skin.text.paint=su::Paint::solid(su::Color::fromRgb8(0xb8b8b8));
 skin.control=su::Paint::solid(su::Color::fromRgb8(0x0e0e0e));
 skin.hovered=su::Paint::solid(su::Color::fromRgb8(0x292929));
 skin.active=su::Paint::solid(su::Color::fromRgb8(0x333d4b));
 skin.accent=su::Paint::solid(su::Color::fromRgb8(0x066ce7));
 skin.cornerRadius=3.f;
 return skin;
}
su::Key toKey(WPARAM vk,LPARAM lp){
 switch(vk){
 case VK_SPACE:return su::Key::Space;
 case VK_RETURN:return su::Key::Enter;
 case VK_ESCAPE:return su::Key::Escape;
 case VK_TAB:return su::Key::Tab;
 case VK_BACK:return su::Key::Backspace;
 case VK_DELETE:return su::Key::Delete;
 case VK_LEFT:return su::Key::Left;
 case VK_RIGHT:return su::Key::Right;
 case VK_UP:return su::Key::Up;
 case VK_DOWN:return su::Key::Down;
 case VK_HOME:return su::Key::Home;
 case VK_END:return su::Key::End;
 case VK_SHIFT:return ((lp>>16)&0xff)==0x36?su::Key::RightShift:su::Key::LeftShift;
 case VK_LSHIFT:return su::Key::LeftShift;
 case VK_RSHIFT:return su::Key::RightShift;
 case VK_CONTROL:return (lp&0x1000000)?su::Key::RightControl:su::Key::LeftControl;
 case VK_LCONTROL:return su::Key::LeftControl;
 case VK_RCONTROL:return su::Key::RightControl;
 case 'A':return su::Key::A;
 case 'C':return su::Key::C;
 case 'V':return su::Key::V;
 case 'X':return su::Key::X;
 case 'Y':return su::Key::Y;
 case 'Z':return su::Key::Z;
 default:return su::Key::Unknown;
 }
}
void keyboardFocus(su::UiContext& ui,const su::InputState& input){
 if(!input.key(su::Key::Tab).pressed)return;
 constexpr std::array<const char*,18> names{
  "picker.radio.hue","picker.radio.saturation",
  "picker.radio.brightness","picker.radio.red","picker.radio.green",
  "picker.radio.blue","picker.radio.alpha",
  "picker.H","picker.S","picker.V","picker.R","picker.G","picker.B",
  "picker.A","picker.HEX","picker.preview","picker.ok","picker.cancel"};
 const bool rev=input.key(su::Key::LeftShift).down||
                input.key(su::Key::RightShift).down;
 int index=rev?0:(int)names.size()-1;
 for(int i=0;i<(int)names.size();i++)
  if(ui.focused()==su::hashId(names[(size_t)i])){index=i;break;}
 index=(index+(rev?-1:1)+(int)names.size())%(int)names.size();
 ui.focus(su::hashId(names[(size_t)index]));
}
struct alignas(DWORD) ModalTemplate {
 DLGTEMPLATE header{};
 WORD noMenu=0;
 WORD defaultDialogClass=0;
 wchar_t title[64]{};
 ModalTemplate(){
  header.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
  header.dwExtendedStyle=WS_EX_DLGMODALFRAME;
  header.cdit=0;
  header.cx=520;header.cy=320;
  lstrcpynW(title,kDialogTitle,64);
 }
};
static_assert(offsetof(ModalTemplate,title)==
              sizeof(DLGTEMPLATE)+2*sizeof(WORD),
              "Win32 in-memory dialog template must have no extra padding");
// Obtain monitor effective DPI before moving the native modal.
static UINT pickerMonitorDpi(HMONITOR monitor,HWND fallback) noexcept {
 using DpiFn=HRESULT(WINAPI*)(HMONITOR,int,UINT*,UINT*);
 // Shcore is a Windows system DLL; resolve only during picker startup.
 HMODULE lib=LoadLibraryExW(L"shcore.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
 UINT x=0,y=0;
 if(lib){
  auto query=reinterpret_cast<DpiFn>(GetProcAddress(lib,"GetDpiForMonitor"));
  if(query)(void)query(monitor,0,&x,&y);
  FreeLibrary(lib);
 }
 return x?x:(fallback?GetDpiForWindow(fallback):96);
}
// Match AE's client-area scale, including the extra alpha channel width.
 // GetWindowRect/SetWindowPos are outer dimensions; the OS computes nonclient
 // sizes for the exact dialog style and monitor DPI.
static gp::PickerOuterSize nativePickerOuterSize(HWND dialog,UINT dpi) noexcept {
 const auto client=gp::pickerClientSize(dpi);
 RECT r{0,0,client.width,client.height};
 const DWORD style=static_cast<DWORD>(GetWindowLongPtrW(dialog,GWL_STYLE));
 const DWORD ex=static_cast<DWORD>(GetWindowLongPtrW(dialog,GWL_EXSTYLE));
 if(AdjustWindowRectExForDpi(&r,style,FALSE,ex,dpi?dpi:96))
  return {int(r.right-r.left),int(r.bottom-r.top)};
 if(AdjustWindowRectEx(&r,style,FALSE,ex))
  return {int(r.right-r.left),int(r.bottom-r.top)};
 return client;
}
// Sizing/centering is done BEFORE the OS displays the modal window.
void centerDialogOnMonitor(HWND dialog,float& uiScale) noexcept {
 RECT current{}, client{};
 if(!GetWindowRect(dialog,&current)||!GetClientRect(dialog,&client))return;
 // Size to the AE reference client-area scale, including the alpha column.
 int width=0,height=0;
 MONITORINFO info{};
 info.cbSize=sizeof(info);
 HWND owner=GetWindow(dialog,GW_OWNER);
 POINT clickPosition{};
 // Use the monitor containing the user's actual stop double-click.
 // Never guess the foreground HWND or attach to another GUI message loop.
 const bool haveCursor=GetCursorPos(&clickPosition)!=FALSE;
 const HMONITOR monitor=haveCursor?
   MonitorFromPoint(clickPosition,MONITOR_DEFAULTTONEAREST):
   MonitorFromWindow(owner?owner:dialog,MONITOR_DEFAULTTONEAREST);
 if(!GetMonitorInfoW(monitor,&info))return;
 const RECT area=info.rcWork;
 const auto target=nativePickerOuterSize(dialog,pickerMonitorDpi(monitor,dialog));
 width=target.width;height=target.height;
 const int x=area.left+((area.right-area.left)-width)/2;
 const int y=area.top+((area.bottom-area.top)-height)/2;
 SetWindowPos(dialog,nullptr,x,y,width,height,
              SWP_NOZORDER|SWP_NOACTIVATE);
 if(GetClientRect(dialog,&client))
  uiScale=gp::pickerClientScale(client.right-client.left,client.bottom-client.top);
}
// CPU-only Font/SlugUI geometry persists across modal openings.
// GPU and HWND always remain scoped to each Session.
struct PickerCpuAssets {
 su::VectorAtlas atlas{};
 std::unique_ptr<gp::FigmaScene> figma{};
 PickerCpuAssets(){
  std::vector<uint32_t> glyphs;
  glyphs.reserve(96);
  for(uint32_t c=32;c<127;c++)glyphs.push_back(c);
  glyphs.push_back(176);
  if(!atlas.loadSystemFont("Source Sans Pro",400,false,glyphs,"Regular")||
     !atlas.loadSystemFont("Source Sans Pro",600,false,glyphs,"SemiBold")||
     !atlas.loadSystemFont("Source Sans Pro",700,false,glyphs,"Bold"))
   throw std::runtime_error("Source Sans Pro not installed");
  figma=std::make_unique<gp::FigmaScene>(atlas);
  atlas.build();
 }
};
PickerCpuAssets& cachedPickerCpuAssets(){
 static PickerCpuAssets result;
 return result;
}
struct Session {
 HWND window=nullptr;
 HWND keyboardProxy=nullptr;
 float uiScale=1.f;
 HMONITOR activeMonitor=nullptr;
 bool monitorReady=false;
 HINSTANCE module=nullptr;
 gp::PickerState model{};
 su::InputState input{};
 su::InputWriter writer{input};
 su::UiContext ui{makeSkin()};
 su::DrawList inputScratch{};
 PickerCpuAssets* cpuAssets=nullptr;
 gp::FigmaScene* figma=nullptr;
 std::unique_ptr<su::Win32PlatformSurface> surface{};
 std::unique_ptr<su::VulkanRenderer> renderer{};
 su::DrawList base{};
 su::Vec2 lastSize{};
 HCURSOR eyedropperCursor=nullptr,scrubCursor=nullptr;
 bool ready=false,closing=false,capture=false;
 bool frameActive=false,repaintPending=false,previewActive=false,caretTimer=false;
 bool previousEye=false,eyeArmed=false;
 gp::PickerState beforeEye{};
 gradation::stopui::ColorPreviewCallback onLive=nullptr;
 void* liveContext=nullptr;
 gradation::stopui::RGBA lastPublished{};
 std::chrono::steady_clock::time_point lastPublishedAt{};
 std::exception_ptr error{};

 explicit Session(gradation::stopui::RGBA rgba,gradation::stopui::Depth depth,
                  gradation::stopui::ColorPreviewCallback live,void* context,
                  int themeIndex)
    :onLive(live),liveContext(context),lastPublished(rgba) {
  model.initial=rgba;model.current=rgba;
  model.hsv=gradation::stopui::toHSV(rgba);
  model.depth=depth;model.theme=std::clamp(themeIndex,0,2);model.sync();
 }
 // Never call through an AE suite on a GPU/render thread; this runs only
 // from the native UI-thread dialog callback while PF_Cmd_EVENT is active.
 void sendPreview(bool force=false) noexcept {
  if(!onLive||model.invalidDraft||closing||!ready)return;
  const auto target=model.finished
    ?(model.accepted?model.current:model.initial)
    :(model.preview?model.current:model.initial);
  if(target.r==lastPublished.r&&target.g==lastPublished.g&&
     target.b==lastPublished.b&&target.a==lastPublished.a){
   KillTimer(window,3);return;
  }
  const auto now=std::chrono::steady_clock::now();
  // Coalesce live AE renders, retaining the last edit even if input stops.
  // Never recursively enter AE from a nested modal message.
  if(previewActive||(!model.finished&&lastPublishedAt.time_since_epoch().count()!=0&&
     now-lastPublishedAt<std::chrono::milliseconds(33))){
   SetTimer(window,3,33,nullptr);return;
  }
  KillTimer(window,3);
  lastPublished=target;
  previewActive=true;
  onLive(liveContext,target);
  previewActive=false;
  lastPublishedAt=std::chrono::steady_clock::now();
  (void)force; // Final OK/Cancel commits in PF_Cmd_EVENT.

 }
 // Called only from the modal's own UI thread. Moving the color picker
 // from one DPI to another uses the exact native reference window dimensions.
 void updateMonitorScale() noexcept {
  if(!monitorReady||!window||!IsWindow(window)||IsIconic(window)||closing)return;
  const HMONITOR next=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
  if(!next||next==activeMonitor)return;
  MONITORINFO info{};info.cbSize=sizeof(info);
  if(!GetMonitorInfoW(next,&info))return;
  const auto target=nativePickerOuterSize(window,pickerMonitorDpi(next,window));
  RECT outer{},client{};
  if(!GetWindowRect(window,&outer)||!GetClientRect(window,&client))return;
  const int w=target.width,h=target.height;
  const RECT work=info.rcWork;
  const int x=std::clamp(outer.left,work.left,
                         std::max(work.left,work.right-w));
  const int y=std::clamp(outer.top,work.top,
                         std::max(work.top,work.bottom-h));
  activeMonitor=next;
  SetWindowPos(window,nullptr,x,y,w,h,
     SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
  if(GetClientRect(window,&client))
   uiScale=gp::pickerClientScale(client.right-client.left,client.bottom-client.top);
  if(ready){
   // Cursors are private size-specific resources; replace both together.
   if(eyedropperCursor)DestroyCursor(eyedropperCursor);
   if(scrubCursor)DestroyCursor(scrubCursor);
   const int cursorSize=std::max(8,int(std::lround(32.f*uiScale)));
   eyedropperCursor=static_cast<HCURSOR>(LoadImageW(
     module,MAKEINTRESOURCEW(101),IMAGE_CURSOR,
     cursorSize,cursorSize,LR_DEFAULTCOLOR));
   scrubCursor=static_cast<HCURSOR>(LoadImageW(
     module,MAKEINTRESOURCEW(102),IMAGE_CURSOR,
     cursorSize,cursorSize,LR_DEFAULTCOLOR));
   InvalidateRect(window,nullptr,FALSE);
  }
 }
 void endWindowInput() noexcept {
  ready=false;
  if(window && IsWindow(window)){
   KillTimer(window,1);
   KillTimer(window,2);
   KillTimer(window,3);
   caretTimer=false;
   if(capture&&GetCapture()==window)ReleaseCapture();
  }
  capture=false;
 }
 void cleanup() noexcept {
  endWindowInput();
  // Crucial: never call vkDeviceWaitIdle or destroy GPU resources
  // from WM_DESTROY, WM_CLOSE, or the EndDialog callback. This is
  // deferred until the OS modal manager has returned to our caller
  // and restored the AE owner window.
  renderer.reset();
  surface.reset();
  figma=nullptr;cpuAssets=nullptr;
  // LoadImage creates private size-matched cursor copies, not shared cursors.
  if(eyedropperCursor){DestroyCursor(eyedropperCursor);eyedropperCursor=nullptr;}
  if(scrubCursor){DestroyCursor(scrubCursor);scrubCursor=nullptr;}
 }
 ~Session(){cleanup();}
 void initialize(HWND h) {
  window=h;
  const bool profile=GetEnvironmentVariableW(L"GRADATION_PICKER_PROFILE",
                                              nullptr,0)!=0;
  using Clock=std::chrono::steady_clock;
  const auto started=Clock::now();
  // The native dialog was already pixel-sized and centered in WM_INITDIALOG.
  cpuAssets=&cachedPickerCpuAssets();
  figma=cpuAssets->figma.get();
  model.metricsAtlas=&cpuAssets->atlas;
  figma->resetRendererResources();
  const auto cpuReady=Clock::now();
  surface=std::make_unique<su::Win32PlatformSurface>(h,module);
  su::RendererConfig conf=gp::pickerRendererConfig();
  conf.enableStartupProfiling=profile;
  renderer=std::make_unique<su::VulkanRenderer>(*surface,cpuAssets->atlas,conf);
  if(profile){
   const auto gpuReady=Clock::now();
   const auto ms=[](Clock::duration d){
    return std::chrono::duration<double,std::milli>(d).count();
   };
   char report[240]{};
   std::snprintf(report,sizeof(report),
     "Gradation Picker startup CPU/Figma %.2fms; Vulkan/window %.2fms; total %.2fms\n",
     ms(cpuReady-started),ms(gpuReady-cpuReady),ms(gpuReady-started));
   OutputDebugStringA(report);
  }
  const int cursorDimension=std::max(8,int(std::lround(32.f*uiScale)));
  eyedropperCursor=static_cast<HCURSOR>(LoadImageW(module,MAKEINTRESOURCEW(101),
         IMAGE_CURSOR,cursorDimension,cursorDimension,LR_DEFAULTCOLOR));
  scrubCursor=static_cast<HCURSOR>(LoadImageW(module,MAKEINTRESOURCEW(102),
         IMAGE_CURSOR,cursorDimension,cursorDimension,LR_DEFAULTCOLOR));
  ui.setInputOnly(true);
  ui.setClipboardCallbacks({
    [this](){return readClipboardUtf8(window);},
    [this](std::string_view text){writeClipboardUtf8(window,text);}
  });
  ui.focus(su::hashId("picker.HEX"));
  ui.selectAllText(su::hashId("picker.HEX"),model.fields[7]);
  ready=true;
 }
 void syncCaretTimer() noexcept {
  const HWND focus=GetFocus();
  const bool focused=focus==window||focus==keyboardProxy;
  const UINT blink=GetCaretBlinkTime();
  const bool wanted=focused&&(model.hexFocused||model.numericFocus>=0)&&blink!=INFINITE;
  if(wanted&&!caretTimer)caretTimer=SetTimer(window,2,std::max<UINT>(100,blink),nullptr)!=0;
  else if(!wanted&&caretTimer){KillTimer(window,2);caretTimer=false;}
 }
 void frame(bool forcedRepaint=false){
  if(!ready||closing||model.finished)return;
  if(frameActive){repaintPending=true;return;}
  const bool forcePreview=input.mouse(su::MouseButton::Left).released;
  {
   frameActive=true;
   struct FrameExit { Session& self; ~FrameExit(){
    self.frameActive=false;
    if(self.repaintPending){self.repaintPending=false;
     if(self.ready&&!self.closing&&self.window)InvalidateRect(self.window,nullptr,FALSE);
    }
   }} exit{*this};
   drawFrame(forcedRepaint);
  }
  // SDK callbacks may pump Windows messages. No GPU/UI draw state is
  // in flight when invoking the host, and recursive preview is coalesced.
  if(ready&&!closing){syncCaretTimer();sendPreview(model.finished||forcePreview);}
 }
 void drawFrame(bool forcedRepaint){
  if(!window||IsIconic(window)||!surface->visible())return;
  auto fb=surface->framebufferSize();
  if(fb.x<=0||fb.y<=0)return;
  writer.finishFrame(timeNow());
  // Figma geometry and invisible hit targets scale uniformly to the
  // actual client extent of the reference-sized native modal.
  const float scale=uiScale;
  keyboardFocus(ui,input);
  // SlugUI widget interaction is processed invisibly. Source Figma
  // nodes are the only visible button and text visuals.
  inputScratch.clear();
  ui.beginFrame(input,inputScratch);
  gp::drawPicker(inputScratch,ui,input,model,scale);
  ui.endFrame();
  if(model.eyedropper&&!previousEye){
   beforeEye.current=model.current;
   beforeEye.hsv=model.hsv;
   eyeArmed=false;
   if(GetCapture()!=window){SetCapture(window);capture=true;}
   SetCursor(eyedropperCursor?eyedropperCursor:LoadCursorA(nullptr,IDC_CROSS));
   SetTimer(window,1,33,nullptr);
  }else if(!model.eyedropper&&previousEye){
   KillTimer(window,1);
   if(GetCapture()==window)ReleaseCapture();
   capture=false;
   SetCursor(LoadCursorA(nullptr,IDC_ARROW));
  }
  previousEye=model.eyedropper;
  if(model.finished)return;
  // Only color changes upload the 32f plane/bar. Hue uses original Figma
  // fills, other axes use renderer-owned seamless raster surfaces.
  const bool rasterChanged=figma->uploadRaster(*renderer,model);
  const bool sceneChanged=figma->update(model);
  // Skip redundant mouse/keyboard frames, but always redraw for OS paint
  // and swapchain-size changes, even when the UI model is unchanged.
  if(!gp::needsPickerFrame(sceneChanged||rasterChanged,forcedRepaint,fb,lastSize))return;
  renderer->prepareFrame();
  // SlugUI's authored transforms produce ProjectedDrawListCommands.
  // They require Slugia's ordinary frame path, not retained GPU lists.
  // Re-record in the reusable buffer so borrowed glyph text/runs stay valid
  // through draw(), including after text edits and WM_PAINT/WM_SIZE.
  base.clear();
  base.roundedRect({0.f,0.f,fb.x,fb.y},0.f,
      su::Paint::solid(su::Color::fromRgb8(gp::pickerPalette(model.theme).background)));
  figma->render(input,base,{0.f,0.f,fb.x,fb.y},scale);
  gp::submitPickerDrawList(*renderer,base);
  figma->markSubmitted(renderer->stats().lastSubmittedSerial);
  lastSize=fb;
 }
 void inputMessage(UINT message,WPARAM wp,LPARAM lp){
  // Clicking into the custom editor must direct keyboard focus to this
  // dialog itself (there are deliberately no child EDIT controls).
  if(message==WM_LBUTTONDOWN&&GetFocus()!=window&&
     GetFocus()!=keyboardProxy)
   SetFocus(window);
  writer.beginFrame();
  if(message==WM_MOUSEMOVE||message==WM_LBUTTONDOWN||
     message==WM_LBUTTONUP||message==WM_RBUTTONDOWN||
     message==WM_RBUTTONUP){
   writer.cursor({(float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp)});
   if(message==WM_LBUTTONDOWN){SetCapture(window);capture=true;}
   if(message==WM_LBUTTONUP&&capture&&!model.eyedropper){ReleaseCapture();capture=false;}
   if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)
    writer.mouseButton(su::MouseButton::Left,
      message==WM_LBUTTONDOWN?su::InputAction::Press:
                               su::InputAction::Release);
   if(message==WM_RBUTTONDOWN||message==WM_RBUTTONUP)
    writer.mouseButton(su::MouseButton::Right,
      message==WM_RBUTTONDOWN?su::InputAction::Press:
                                su::InputAction::Release);
  }else if(message==WM_KEYDOWN||message==WM_KEYUP||
            message==WM_SYSKEYDOWN||message==WM_SYSKEYUP){
   const su::Key key=toKey(wp,lp);
   const bool keyUp=message==WM_KEYUP||message==WM_SYSKEYUP;
   if(key!=su::Key::Unknown)
    writer.key(key,keyUp?su::InputAction::Release:
      (lp&(1ll<<30))?su::InputAction::Repeat:su::InputAction::Press);
   if(!keyUp&&key==su::Key::Escape&&!(lp&(1ll<<30))){
    if(model.eyedropper){
     model.current=beforeEye.current;
     model.hsv=beforeEye.hsv;
     model.eyedropper=false;model.sync();
    }else{model.finished=true;model.accepted=false;}
   }
  }else if(message==WM_CHAR){
   if(wp>=32&&wp<=0x10ffff&&!(wp>=0xd800&&wp<=0xdfff))
    writer.codepoint((uint32_t)wp);
  }else if(message==WM_MOUSEWHEEL){
   writer.scroll({0.f,(float)GET_WHEEL_DELTA_WPARAM(wp)/120.f},timeNow());
  }else if(message==WM_KILLFOCUS&&
       reinterpret_cast<HWND>(wp)!=keyboardProxy)writer.focusLost();
  // Mouse-move traffic must not reset a blinking text caret.
  if(message==WM_KEYDOWN||message==WM_CHAR||
     message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN)
    model.caretVisible=true;
  frame();
  // Native EDIT child receives OS keyboard focus while Slugia owns all
  // visible text, selection and numeric parsing; zero child pixels are seen.
  if((message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)&&keyboardProxy&&
     (model.numericFocus>=0||model.hexFocused)&&GetFocus()!=keyboardProxy)
   SetFocus(keyboardProxy);
  // Update cursor immediately on the same mouse event that enters
  // text-edit or scrub mode; no extra WM_MOUSEMOVE is required.
  if(message==WM_LBUTTONDOWN||message==WM_MOUSEMOVE)
   SendMessageW(window,WM_SETCURSOR,reinterpret_cast<WPARAM>(window),
                MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));
  if(model.finished&&!closing){
   closing=true;
   EndDialog(window,model.accepted?IDOK:IDCANCEL);
  }
 }
 void eyedropperTick(){
  if(!model.eyedropper)return;
  writer.beginFrame();
  const bool down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
  if(!down)eyeArmed=true;
  POINT pt{};
  if(GetCursorPos(&pt)){
   HDC dc=GetDC(nullptr);
   if(dc){
    const COLORREF px=GetPixel(dc,pt.x,pt.y);
    ReleaseDC(nullptr,dc);
    if(px!=CLR_INVALID){
     const float alpha=model.current.a;
     model.current={GetRValue(px)/255.f,GetGValue(px)/255.f,
                    GetBValue(px)/255.f,alpha};
     model.hsv=gp::deriveHSV(model.current,model.hsv);
     model.sync(ui.focused());
    }
   }
  }
  if(down&&eyeArmed)model.eyedropper=false;
  if(model.eyedropper&&GetCapture()==window){
   // Mouse capture keeps the custom cursor even over other windows
   // during a deliberate screen-pick session; no system-wide cursor override.
   SetCursor(eyedropperCursor?eyedropperCursor:LoadCursorA(nullptr,IDC_CROSS));
  }
  frame();
 }
 INT_PTR dispatch(UINT msg,WPARAM wp,LPARAM lp){
  switch(msg){
   // DialogBoxIndirect's keyboard navigation would otherwise consume
   // numerals/character keys: this no-child custom editor is the focus HWND.
   case WM_GETDLGCODE:
    // A DLGPROC returns handled=TRUE and writes its actual message result
    // into DWLP_MSGRESULT. Returning DLGC_* bits directly loses the flags.
    return gp::handlePickerGetDlgCode(window);
   case WM_INITDIALOG: return FALSE;
   case WM_CLOSE:
    model.accepted=false;model.finished=true;closing=true;
    EndDialog(window,IDCANCEL);return TRUE;
   case WM_COMMAND:
    if(LOWORD(wp)==IDOK||LOWORD(wp)==IDCANCEL){
     model.accepted=(LOWORD(wp)==IDOK)&&!model.invalidDraft;
     model.finished=true;closing=true;
     EndDialog(window,model.accepted?IDOK:IDCANCEL);
     return TRUE;
    }
    break;
   case WM_ERASEBKGND:return TRUE;
   case WM_PAINT:{
    {
    PAINTSTRUCT paint{};
    HDC dc=BeginPaint(window,&paint);
    struct PaintEnd { HWND window; PAINTSTRUCT& paint; ~PaintEnd(){EndPaint(window,&paint);} } end{window,paint};
    if(!ready&&dc){
     // A complete, numerically accurate CPU first paint is available
     // before Vulkan instance/device/pipelines exist. Do not expose an
     // empty black modal during a slow first GPU initialization.
     RECT rc{};
     GetClientRect(window,&rc);
     gp::drawPickerBootstrap(dc,model,rc.right-rc.left,rc.bottom-rc.top);
    }
    } // EndPaint before any Vulkan work.
    if(ready){writer.beginFrame();frame(true);}
    return TRUE;
   }
   case WM_WINDOWPOSCHANGED:
    // Leave default dispatch intact so Windows still generates WM_SIZE
    // and WM_MOVE for the native modal and handles DPI negotiation.
    updateMonitorScale();
    return FALSE;
   case WM_MOVE:
    updateMonitorScale();
    return FALSE;
   case WM_DPICHANGED:{
    const auto target=nativePickerOuterSize(window,HIWORD(wp));
    const RECT* suggested=reinterpret_cast<const RECT*>(lp);
    if(suggested){
     activeMonitor=MonitorFromRect(suggested,MONITOR_DEFAULTTONEAREST);
     SetWindowPos(window,nullptr,suggested->left,suggested->top,
                  target.width,target.height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    return TRUE;
   }
   case WM_SIZE:
    if(LOWORD(lp)&&HIWORD(lp))
     uiScale=gp::pickerClientScale(LOWORD(lp),HIWORD(lp));
    if(ready)InvalidateRect(window,nullptr,FALSE);
    return TRUE;
   case WM_MOUSEMOVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:
   case WM_RBUTTONDOWN:case WM_RBUTTONUP:
   case WM_KEYDOWN:case WM_KEYUP:case WM_SYSKEYDOWN:case WM_SYSKEYUP:
   case WM_CHAR:case WM_MOUSEWHEEL:case WM_KILLFOCUS:
    if(ready)inputMessage(msg,wp,lp);
    return TRUE;
   case WM_TIMER:
    if(wp==3){KillTimer(window,3);if(ready&&!closing)sendPreview();return TRUE;}
    if(wp==1&&ready){eyedropperTick();return TRUE;}
    if(wp==2&&ready){
     if(model.hexFocused||model.numericFocus>=0){
      model.caretVisible=!model.caretVisible;
      writer.beginFrame();frame();
     }
     return TRUE;
    }
    break;
   case WM_SETCURSOR:
    if(LOWORD(lp)==HTCLIENT){
     POINT pt{};
     if(GetCursorPos(&pt)&&ScreenToClient(window,&pt)){
      const float x=float(pt.x)/uiScale,y=float(pt.y)/uiScale;
      bool number= model.scrubActive;
      constexpr std::array<float,7> rowY{145,177,209,249,281,313,345};
      bool editing=false;
      for(int i=0;i<7;++i){
       const float width=gp::pickerNumericWidth(model.metricsAtlas,model.fields[(size_t)i]);
       if(model.numericFocus==i){
        const bool inside=x>=592.f&&x<592.f+std::max(72.f,width+17.f)&&
          y>=rowY[(size_t)i]-4.f&&y<rowY[(size_t)i]+28.f;
        editing=inside;
        number=number||inside;
       }else if(x>=601.f&&x<601.f+width&&
                y>=rowY[(size_t)i]&&y<rowY[(size_t)i]+24.f)
        number=true;
      }
      if(model.eyedropper||
         (x>=687.f&&x<=719.f&&y>=165.f&&y<=197.f)){
       SetCursor(eyedropperCursor?eyedropperCursor:LoadCursorA(nullptr,IDC_CROSS));
       return TRUE;
      }
      if(number){
       if(editing&&!model.scrubActive)
        SetCursor(LoadCursorA(nullptr,IDC_IBEAM));
       else SetCursor(scrubCursor?scrubCursor:LoadCursorA(nullptr,IDC_SIZEWE));
       return TRUE;
      }
      if(x>=571.f&&x<653.f&&y>=384.f&&y<416.f){
       SetCursor(LoadCursorA(nullptr,IDC_IBEAM));return TRUE;
      }
     }
    }
    break;
   case WM_CAPTURECHANGED:capture=false;return TRUE;
   case WM_DESTROY:
    endWindowInput();return TRUE;
   case WM_NCDESTROY:
    SetWindowLongPtrW(window,DWLP_USER,0);
    window=nullptr;return FALSE;
  }
  return FALSE; // DefDlgProc handles regular focus/activation
 }
 static INT_PTR CALLBACK dialogProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
  Session* self=nullptr;
  if(msg==WM_INITDIALOG){
   self=reinterpret_cast<Session*>(lp);
   if(!self)return FALSE;
   self->window=h;
   SetWindowLongPtrW(h,DWLP_USER,reinterpret_cast<LONG_PTR>(self));
   centerDialogOnMonitor(h,self->uiScale);
   self->activeMonitor=MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST);
   self->monitorReady=true;
   SetFocus(h);
   // Focus proxy stays off-screen, but its native EDIT class ensures the
   // modal loop really dispatches WM_KEYDOWN/WM_CHAR despite AE shortcuts.
   self->keyboardProxy=gp::createPickerKeyboardProxy(h,self->module);
   // Show the *actual, centered native modal immediately*, before ANY
   // atlas construction or Vulkan device/swapchain creation. The OS modal
   // manager still owns the parent-disabled state and the message loop.
   // WM_PAINT provides a matching dark background until the first GPU frame.
   ShowWindow(h,SW_SHOWNORMAL);
   UpdateWindow(h);
   // GPU initialization in WM_INITDIALOG hides failures before the
   // OS can show the modal. Queue it for after dialog activation.
   // No AE message loop is touched; DialogBox owns message routing.
   if(!PostMessageW(h,WM_START_PICKER,0,0)){
    self->error=std::make_exception_ptr(
        std::runtime_error("Cannot queue Color Picker initialization"));
    EndDialog(h,IDCANCEL);
   }
   return FALSE; // SetFocus called explicitly, preserve the native focus HWND.
  }
  self=reinterpret_cast<Session*>(GetWindowLongPtrW(h,DWLP_USER));
  if(!self)return FALSE;
  try{
   if(msg==WM_START_PICKER){
    if(self->ready||self->closing)return TRUE;
    using Clock=std::chrono::steady_clock;
    const auto startupBegin=Clock::now();
    self->initialize(h);
    const auto startupReady=Clock::now();
    self->writer.beginFrame();
    self->frame();
    if(GetEnvironmentVariableW(L"GRADATION_PICKER_PROFILE",nullptr,0)!=0){
     const auto firstSubmitted=Clock::now();
     char report[230]{};
     const auto elapsed=[](Clock::duration d){
      return std::chrono::duration<double,std::milli>(d).count();
     };
     std::snprintf(report,sizeof(report),
       "Gradation picker: renderer-init=%.2fms first-GPU-frame=%.2fms total-after-dialog=%.2fms\\n",
       elapsed(startupReady-startupBegin),
       elapsed(firstSubmitted-startupReady),
       elapsed(firstSubmitted-startupBegin));
     OutputDebugStringA(report);
    }
    return TRUE;
   }
   return self->dispatch(msg,wp,lp);
  }
  catch(...){
   self->error=std::current_exception();
   self->endWindowInput();
   EndDialog(h,IDCANCEL);
   return TRUE;
  }
 }
 bool open(HWND owner,gradation::stopui::RGBA& selected,
           std::string* failureReason){
  HMODULE moduleH=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
       reinterpret_cast<LPCWSTR>(&Session::dialogProc),&moduleH)){
    if(failureReason)*failureReason="Cannot locate the Gradation AEX module";
    return false;
   }
  module=(HINSTANCE)moduleH;
  ModalTemplate templ{};
  // AE's official UtilitySuite6 provides the owner. If it was closed
  // or moved to another thread after validation, fail without showing.
  if(!owner||!IsWindow(owner)){
   if(failureReason)*failureReason="AE's main window is not valid";
   return false;
  }
  DWORD ownerPid=0;
  const DWORD ownerThread=GetWindowThreadProcessId(owner,&ownerPid);
  if(!ownerThread||ownerPid!=GetCurrentProcessId()){
   if(failureReason)*failureReason="AE's main window is not in this process";
   return false;
  }
  if(ownerThread!=GetCurrentThreadId()){
   // Cross-thread owned modal windows couple input queues and could
   // deadlock the host. Do not guess another HWND or start another loop.
   if(failureReason)*failureReason=
      "Effect Controls event and AE main window use different UI threads";
   return false;
  }
  // No custom parent EnableWindow / GetMessageW. Windows owns both.
  SetLastError(ERROR_SUCCESS);
  const INT_PTR result=DialogBoxIndirectParamW(
    module,reinterpret_cast<LPCDLGTEMPLATEW>(&templ),owner,
    &Session::dialogProc,reinterpret_cast<LPARAM>(this));
  const DWORD win32Error=GetLastError();
  cleanup();
  if(error)std::rethrow_exception(error);
  if(result==-1||result==0){
   if(failureReason)
    *failureReason="Windows failed to create the modal dialog (Win32 "+
           std::to_string(win32Error)+")";
   return false;
  }
  if(result!=IDOK)return false;
  selected=model.current; // commit only on OK
  return true;
 }
};
} // anonymous

namespace gradation::stopui {
bool editRGBA(HWND owner,RGBA& value,Depth depth,
              std::string* failureReason,
              ColorPreviewCallback live,void* context,
              int themeIndex) noexcept {
 if(failureReason)failureReason->clear();
 if(!owner){
  if(failureReason){try{*failureReason="AE main window handle is missing";}catch(...){}}
  return false;
 }
 // One picker per module, even if the host calls UI events on multiple
 // threads. Atomic failure returns without blocking AE or opening a
 // second modal. SmartFX/MFR render workers never enter this path.
 static std::atomic_flag pickerOpen=ATOMIC_FLAG_INIT;
 if(pickerOpen.test_and_set(std::memory_order_acquire)){
  if(failureReason){try{*failureReason="Color Picker is already open";}catch(...){}}
  return false;
 }
 struct Reset {
  std::atomic_flag& flag;
  ~Reset(){flag.clear(std::memory_order_release);}
 } reset{pickerOpen};
 try{
  Session s(value,depth,live,context,themeIndex);
  return s.open(owner,value,failureReason);
 }catch(const std::exception& ex){
  if(failureReason){
   try{*failureReason=ex.what();}
   catch(...){}
  }
  OutputDebugStringA(ex.what());
  return false;
 }catch(...){
  if(failureReason){
   try{*failureReason="Unknown exception in Color Picker initialization";}
   catch(...){}
  }
  return false;
 }
}
} // gradation::stopui
