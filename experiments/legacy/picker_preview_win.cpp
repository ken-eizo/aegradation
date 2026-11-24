// Launch ONLY when the user opens this standalone preview executable.
// This does not start Adobe After Effects, install an AEX or modify files.
#include "stop_rgba_dialog_win.hpp"
#include <windows.h>
#include <cwchar>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 using namespace gradation::stopui;
 RGBA color{0.91f,0.16f,0.29f,0.64f};
 if(editRGBA(nullptr,color,Depth::Bpc32)){
  wchar_t result[200]{};
  std::swprintf(result,200,
    L"Selected RGB / Alpha:\nR %.6f\nG %.6f\nB %.6f\nAlpha %.6f\nHex #%ls",
    (double)color.r,(double)color.g,(double)color.b,
    (double)color.a,hexRGB(color).c_str());
  MessageBoxW(nullptr,result,L"Gradation Color Picker Preview",
              MB_OK|MB_ICONINFORMATION);
 }
 return 0;
}
