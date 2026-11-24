#pragma once
// Optional on-demand Slugia color picker bridge for Gradation's Win32 UI.
// No GPU, Vulkan, Slugia or GLFW symbols are linked into the AE AEX.
// The helper executable must sit beside Gradation.aex.
// Missing/broken helper -> existing native picker fallback;
// user Cancel -> no color change and NO fallback.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>
#include <iterator>
#include "../../src/ui/common/stop_value_codec.hpp"
#include "../../src/ui/common/stop_color_picker_model.hpp"

namespace gradation::stopui {
enum class SlugiaPickerResult {Unavailable,Accepted,Cancelled};

inline std::wstring quoteWin32(const std::wstring& value) {
 std::wstring result=L"\"";
 size_t backslashes=0;
 for(const wchar_t c:value){
  if(c==L'\\'){++backslashes;continue;}
  if(c==L'"'){
   result.append(backslashes*2+1,L'\\');result+=L'"';
  }else{
   result.append(backslashes,L'\\');result+=c;
  }
  backslashes=0;
 }
 result.append(backslashes*2,L'\\');result+=L'"';
 return result;
}
inline std::wstring formatTransfer(float x){
 wchar_t buf[64]{};
 std::swprintf(buf,64,L"%.9g",(double)x);
 return buf;
}
inline SlugiaPickerResult editRGBAWithSlugia(HWND owner,RGBA& value,
                                            Depth depth)noexcept {
 try{
  if(!std::isfinite(value.r)||!std::isfinite(value.g)||
     !std::isfinite(value.b)||!std::isfinite(value.a))
   return SlugiaPickerResult::Unavailable;
  HMODULE plugin=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&editRGBAWithSlugia),&plugin))
   return SlugiaPickerResult::Unavailable;
  wchar_t modulePath[32768]{};
  const DWORD count=GetModuleFileNameW(plugin,modulePath,
                                (DWORD)std::size(modulePath));
  if(!count||count>=std::size(modulePath))return SlugiaPickerResult::Unavailable;
  const auto executable=std::filesystem::path(modulePath).parent_path()/
                       L"GradationSlugiaPicker.exe";
  if(!std::filesystem::is_regular_file(executable))
   return SlugiaPickerResult::Unavailable;
  wchar_t tmpDir[MAX_PATH]{};
  wchar_t resultFile[MAX_PATH]{};
  if(!GetTempPathW(MAX_PATH,tmpDir)||
     !GetTempFileNameW(tmpDir,L"GCP",0,resultFile))
   return SlugiaPickerResult::Unavailable;
  struct Cleanup {
   wchar_t* path;
   ~Cleanup(){if(path&&*path)DeleteFileW(path);}
  } cleanup{resultFile};
  const HWND root=owner?GetAncestor(owner,GA_ROOT):nullptr;
  std::wstring command=quoteWin32(executable.wstring());
  command+=L" --r "+formatTransfer(value.r);
  command+=L" --g "+formatTransfer(value.g);
  command+=L" --b "+formatTransfer(value.b);
  command+=L" --a "+formatTransfer(value.a);
  command+=L" --depth "+std::to_wstring(
         depth==Depth::Bpc8?8:depth==Depth::Bpc16?16:32);
  command+=L" --owner "+std::to_wstring((uintptr_t)root);
  command+=L" --result "+quoteWin32(resultFile);
  std::vector<wchar_t> commandLine(command.begin(),command.end());
  commandLine.push_back(L'\0');
  STARTUPINFOW start{};start.cb=sizeof(start);
  PROCESS_INFORMATION process{};
  if(!CreateProcessW(executable.c_str(),commandLine.data(),
       nullptr,nullptr,FALSE,0,nullptr,executable.parent_path().c_str(),
       &start,&process))
   return SlugiaPickerResult::Unavailable;
  CloseHandle(process.hThread);
  const bool disabled=root&&IsWindowEnabled(root);
  if(disabled)EnableWindow(root,FALSE);
  DWORD code=STILL_ACTIVE;
  for(;;){
   const DWORD rc=MsgWaitForMultipleObjects(1,&process.hProcess,FALSE,
                                          INFINITE,QS_ALLINPUT);
   if(rc==WAIT_OBJECT_0)break;
   if(rc!=WAIT_OBJECT_0+1)break;
   MSG message{};
   while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
    TranslateMessage(&message);
    DispatchMessageW(&message);
   }
  }
  GetExitCodeProcess(process.hProcess,&code);
  CloseHandle(process.hProcess);
  if(disabled&&IsWindow(root)){
   EnableWindow(root,TRUE);
   SetForegroundWindow(root);
  }
  if(code==1)return SlugiaPickerResult::Cancelled;
  if(code!=0)return SlugiaPickerResult::Unavailable;
  std::ifstream in{std::filesystem::path(resultFile)};
  if(!in)return SlugiaPickerResult::Unavailable;
  in.imbue(std::locale::classic());
  RGBA candidate{};
  if(!(in>>candidate.r>>candidate.g>>candidate.b>>candidate.a)||
      !std::isfinite(candidate.r)||!std::isfinite(candidate.g)||
      !std::isfinite(candidate.b)||!std::isfinite(candidate.a))
   return SlugiaPickerResult::Unavailable;
  value=candidate;
  return SlugiaPickerResult::Accepted;
 }catch(...){
  return SlugiaPickerResult::Unavailable;
 }
}
} // namespace gradation::stopui
#endif
