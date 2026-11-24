// Separate diagnostic only, never shipped in Gradation distribution.
// Runs real Windows dialog lifetime cycles around the exact linked
// SlugUI color picker and verifies owner enable-state restoration.
// Must run on the UI thread with an actual active parent window.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include "../stop_rgba_dialog_embedded.hpp"
using namespace std::chrono_literals;
static LRESULT CALLBACK ownerProc(HWND h,UINT m,WPARAM w,LPARAM l){
 return DefWindowProcW(h,m,w,l);
}
int main(int argc,char** argv){
 // Never opens any window; safe for automated CTest and CI.
 if(argc>1&&std::strcmp(argv[1],"--headless")==0){
  gradation::stopui::RGBA original{.2f,.7f,.41f,.63f};
  auto edited=original;
  std::string error;
  const bool changed=gradation::stopui::editRGBA(
      nullptr,edited,gradation::stopui::Depth::Bpc32,&error);
  const bool pass=!changed&&!error.empty()&&
    std::memcmp(&original,&edited,sizeof(original))==0;
  std::cout<<(pass?"PASS":"FAIL")
           <<": invalid owner rejected without creating HWND; reason="
           <<error<<"\n";
  return pass?0:7;
 }
 HINSTANCE inst=GetModuleHandleW(nullptr);
 WNDCLASSW cls{};
 cls.hInstance=inst;cls.lpfnWndProc=&ownerProc;
 cls.lpszClassName=L"GradationSdkDialogOwnerTest";
 if(!RegisterClassW(&cls))return 5;
 HWND owner=CreateWindowW(cls.lpszClassName,L"Gradation Lifetime Owner",
  WS_OVERLAPPEDWINDOW,120,120,500,280,nullptr,nullptr,inst,nullptr);
 if(!owner)return 6;
 ShowWindow(owner,SW_SHOWNORMAL);
 SetForegroundWindow(owner);SetActiveWindow(owner);
 UpdateWindow(owner);
 bool everything=true;
 for(int pass=0;pass<6;pass++){
  const UINT action=pass%3==0?WM_CLOSE:WM_COMMAND;
  std::atomic<bool> seen{false},disabled{false};
  std::thread closer([&](){
   HWND dialog=nullptr;
   for(int tries=0;tries<250;tries++){
    dialog=FindWindowW(L"#32770",L"Gradation - Color & Alpha");
    if(dialog&&IsWindowVisible(dialog)){
     seen=true;disabled=!IsWindowEnabled(owner);break;
    }
    std::this_thread::sleep_for(20ms);
   }
   if(dialog&&seen){
    const DWORD screenshotHold=GetEnvironmentVariableW(
         L"GRAD_MODAL_TEST_CAPTURE",nullptr,0)?1800:70;
    std::this_thread::sleep_for(std::chrono::milliseconds(screenshotHold));
    if(action==WM_CLOSE)PostMessageW(dialog,WM_CLOSE,0,0);
    else PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
   }
  });
  gradation::stopui::RGBA original{.2f,.7f,.41f,.63f};
  auto edited=original;
  const bool ok=gradation::stopui::editRGBA(owner,edited,gradation::stopui::Depth::Bpc32);
  closer.join();
  const bool enabled=IsWindowEnabled(owner)!=FALSE;
  const bool unchanged=std::memcmp(&original,&edited,sizeof(original))==0;
  everything &= seen && disabled && enabled && unchanged && !ok;
  std::cout<<"cycle "<<pass<<" dialog="<<seen<<" parent_disabled_during="
           <<disabled<<" parent_reenabled_after="<<enabled
           <<" cancel_preserves_rgba="<<unchanged<<" result="<<ok<<"\n";
  if(!enabled)break;
 }
 if(IsWindow(owner))DestroyWindow(owner);
 return everything?0:4;
}
