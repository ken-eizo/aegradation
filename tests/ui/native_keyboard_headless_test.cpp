// Invisible Win32 dialog-message test. Never shows a window or starts AE.
// Tests the real DefDlgProc -> DLGPROC -> DWLP_MSGRESULT return contract and
// dispatches WM_CHAR / WM_KEYDOWN through InputWriter and PickerState.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include "../../src/ui/windows/picker_win32_keys.hpp"
#include "../../src/ui/windows/picker_ui.hpp"
#include "../../src/ui/windows/picker_bootstrap_gdi.hpp"
#include <slugia/input.hpp>
#include <slugia/ui.hpp>
#include <slugia/draw_list.hpp>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace su=slugia;
namespace gp=gradation::picker;
namespace {
struct Probe {
 gp::PickerState model{};
 su::InputState input{};
 su::InputWriter writer{input};
 su::UiContext editor{su::UiSkin{}};
 su::DrawList scratch{};
 double tick=0.0;
 Probe(gradation::stopui::Depth depth=gradation::stopui::Depth::Bpc8){
  model.depth=depth;
  model.hsv.h=178.f;
  model.sync();
  editor.setInputOnly(true);
 }
 void feed(UINT msg,WPARAM wp,LPARAM lp){
  writer.beginFrame();
  if(msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP){
   writer.cursor({float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))});
   writer.mouseButton(su::MouseButton::Left,
      msg==WM_LBUTTONDOWN?su::InputAction::Press:su::InputAction::Release);
  }else if(msg==WM_CHAR){
   if(wp>=32&&wp<=0x10ffff)writer.codepoint(static_cast<std::uint32_t>(wp));
  }else if(msg==WM_KEYDOWN||msg==WM_KEYUP){
   su::Key key=su::Key::Unknown;
   switch(wp){
   case VK_BACK:key=su::Key::Backspace;break;
   case VK_DELETE:key=su::Key::Delete;break;
   case VK_CONTROL:key=su::Key::LeftControl;break;
   case 'A':key=su::Key::A;break;
   case VK_LEFT:key=su::Key::Left;break;
   case VK_RIGHT:key=su::Key::Right;break;
   default:break;
   }
   if(key!=su::Key::Unknown)writer.key(key,msg==WM_KEYDOWN?
        su::InputAction::Press:su::InputAction::Release);
  }
  tick+=.016;
  writer.finishFrame(tick);
  scratch.clear();
  editor.beginFrame(input,scratch);
  gp::drawPicker(scratch,editor,input,model,1.f);
  editor.endFrame();
  if(!scratch.commands().empty()||!scratch.overlayCommands().empty())
   throw std::runtime_error("Input-only editor emitted draw commands");
 }
};
struct alignas(DWORD) HiddenTemplate {
 DLGTEMPLATE header{};
 WORD menu=0,klass=0;
 wchar_t title[2]{};
 HiddenTemplate(){
  header.style=WS_POPUP|WS_CAPTION|DS_MODALFRAME;
  header.dwExtendedStyle=0;
  header.cdit=0;header.cx=100;header.cy=100;
 }
};
static_assert(offsetof(HiddenTemplate,title)==sizeof(DLGTEMPLATE)+4);
INT_PTR CALLBACK probeDlgProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 if(msg==WM_INITDIALOG){
  SetWindowLongPtrW(hwnd,DWLP_USER,lp);
  return FALSE;
 }
 if(msg==WM_GETDLGCODE)return gp::handlePickerGetDlgCode(hwnd);
 auto* p=reinterpret_cast<Probe*>(GetWindowLongPtrW(hwnd,DWLP_USER));
 if(p&&(msg==WM_CHAR||msg==WM_KEYDOWN||msg==WM_KEYUP||
        msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP)){
  p->feed(msg,wp,lp);
  return TRUE;
 }
 return FALSE;
}
void require(bool condition,const char* message){
 if(!condition)throw std::runtime_error(message);
}
void mouse(HWND hwnd,int x,int y){
 SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
 SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));
}
void type(HWND hwnd,const char* s){
 for(;*s;++s)SendMessageW(hwnd,WM_CHAR,
                         static_cast<unsigned char>(*s),1);
}
void testDepth(gradation::stopui::Depth depth){
 Probe probe(depth);
 HiddenTemplate templ{};
 HWND hidden=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),
              reinterpret_cast<const DLGTEMPLATE*>(&templ),
              nullptr,probeDlgProc,reinterpret_cast<LPARAM>(&probe));
 require(hidden!=nullptr,"Cannot create non-visible native test dialog");
 const HWND proxy=gp::createPickerKeyboardProxy(hidden,GetModuleHandleW(nullptr));
 require(proxy!=nullptr,"Off-screen native keyboard proxy could not be created");
 try{
  require(!IsWindowVisible(hidden),"Headless test must not show a GUI");
  const MSG key{hidden,WM_KEYDOWN,'7',0,0,{0,0}};
  const LRESULT dlgCode=SendMessageW(hidden,WM_GETDLGCODE,'7',
                                     reinterpret_cast<LPARAM>(&key));
  require((dlgCode&gp::kPickerDlgCode)==gp::kPickerDlgCode,
          "Win32 DefDlgProc dropped DLGC_WANTCHARS / WANTALLKEYS flags");
  const LRESULT childCode=SendMessageW(proxy,WM_GETDLGCODE,'7',
                                       reinterpret_cast<LPARAM>(&key));
  require((childCode&gp::kPickerDlgCode)==gp::kPickerDlgCode,
          "Native EDIT control failed to request character/key delivery");
  require(!IsWindowVisible(hidden),
          "Native key proxy test must never show a real application window");
  mouse(hidden,607,155);
  const auto id=su::hashId("picker.H");
  const auto* e=probe.editor.textEditState(id);
  require(probe.editor.focused()==id&&e&&e->hasSelection(),
          "Click must focus the numeric H editor and select digits");
  type(proxy,"27");
  require(probe.model.fields[0]=="27"&&std::abs(probe.model.hsv.h-27.f)<.001f,
          "Win32 WM_CHAR must replace selection and append following digits");
  SendMessageW(proxy,WM_KEYDOWN,VK_BACK,1);
  SendMessageW(proxy,WM_KEYUP,VK_BACK,1);
  require(probe.model.fields[0]=="2","WM_KEYDOWN Backspace must delete a digit");
  mouse(hidden,776,425);
  require(probe.model.numericFocus<0&&probe.model.fields[0]=="2",
          "Blur must commit edited numeric digits");
  // R uses bit-depth-specific formatting and parsing, not a fixed 8bpc range.
  mouse(hidden,607,260);
  const auto rid=su::hashId("picker.R");
  const auto* red=probe.editor.textEditState(rid);
  require(probe.editor.focused()==rid&&red&&red->hasSelection(),
          "R channel must accept native click+select");
  const char* value=depth==gradation::stopui::Depth::Bpc32?"0.625":
                    depth==gradation::stopui::Depth::Bpc16?"12345":"123";
  type(proxy,value);
  require(!probe.model.invalidDraft,"Valid RGB numeric edit must be accepted");
  mouse(hidden,776,425);
  if(depth==gradation::stopui::Depth::Bpc8)
   require(std::abs(probe.model.current.r-123.f/255.f)<1e-5f,
           "8bpc R field input must use 255");
  else if(depth==gradation::stopui::Depth::Bpc16)
   require(std::abs(probe.model.current.r-12345.f/32768.f)<1e-5f,
           "16bpc R field input must use 32768");
  else
   require(std::abs(probe.model.current.r-.625f)<1e-5f,
           "32bpc R field must accept floating point input");

  // HEX remains a genuine text editor (not auto-select on single click).
  // Ctrl+A selects its six characters; a typed HEX code replaces them.
  mouse(hidden,590,396);
  require(probe.model.hexFocused&&probe.editor.focused()==su::hashId("picker.HEX"),
          "HEX input must receive keyboard focus");
  SendMessageW(proxy,WM_KEYDOWN,VK_CONTROL,1);
  SendMessageW(proxy,WM_KEYDOWN,'A',1);
  SendMessageW(proxy,WM_KEYUP,'A',1);
  SendMessageW(proxy,WM_KEYUP,VK_CONTROL,1);
  const auto* hex=probe.editor.textEditState(su::hashId("picker.HEX"));
  require(hex&&hex->hasSelection()&&hex->selectionBegin()==0&&
          hex->selectionEnd()==probe.model.fields[7].size(),
          "Ctrl+A must select only HEX field text");
  type(proxy,"00FF00");
  require(!probe.model.invalidDraft&&probe.model.fields[7]=="00FF00",
          "Win32 WM_CHAR must replace a selected HEX string");
  mouse(hidden,776,425);
  require(!probe.model.hexFocused&&
          probe.model.current.r==0.f&&probe.model.current.g==1.f&&
          probe.model.current.b==0.f,
          "HEX text commit must update the full RGB value");

  DestroyWindow(hidden);
 }catch(...){DestroyWindow(hidden);throw;}
}
}
int main(){
 try{
  testDepth(gradation::stopui::Depth::Bpc8);
  testDepth(gradation::stopui::Depth::Bpc16);
  testDepth(gradation::stopui::Depth::Bpc32);
  // CPU first paint must contain actual picker controls before Vulkan
  // startup, without ever showing a window.
  HDC dc=CreateCompatibleDC(nullptr);
  require(dc!=nullptr,"GDI off-screen validation DC missing");
  BITMAPINFO info{};
  info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth=797;
  info.bmiHeader.biHeight=-444;
  info.bmiHeader.biPlanes=1;
  info.bmiHeader.biBitCount=32;
  info.bmiHeader.biCompression=BI_RGB;
  void* bytes=nullptr;
  HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bytes,nullptr,0);
  require(bitmap&&bytes,"Off-screen 797x444 test bitmap unavailable");
  const HGDIOBJ prior=SelectObject(dc,bitmap);
  gradation::picker::PickerState first{};
  first.hsv={178.f,.8f,.9f};
  first.current=gradation::stopui::fromHSV(first.hsv);
  first.sync();
  const auto started=std::chrono::steady_clock::now();
  gradation::picker::drawPickerBootstrap(dc,first,797,444);
  const auto finished=std::chrono::steady_clock::now();
  const auto* pixels=static_cast<const std::uint32_t*>(bytes);
  require((pixels[40*797+30]&0x00ffffffu)!=0x001d1d1du,
          "Initial CPU paint left plane blank");
  require((pixels[160*797+450]&0x00ffffffu)!=0x001d1d1du,
          "Initial CPU paint left hue bar blank");
  require((pixels[43*797+700]&0x00ffffffu)!=0x001d1d1du,
          "Initial CPU paint left OK button blank");
  std::cout<<"BENCH CPU first paint (includes 386x386 RGBA raster+GDI): "
           <<std::chrono::duration<double,std::milli>(finished-started).count()
           <<" ms\\n";
  SelectObject(dc,prior);DeleteObject(bitmap);DeleteDC(dc);
  std::cout<<"PASS: hidden Win32 DefDlgProc keyboard flags + mouse/WM_CHAR/Backspace/blur for 8/16/32bpc\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"FAIL: "<<e.what()<<"\n";
  return 1;
 }
}
