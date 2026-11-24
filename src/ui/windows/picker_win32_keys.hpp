#pragma once
// Native Win32 keyboard routing for the Slugia childless-looking color editor.
// The visible UI remains entirely Figma/SlugUI, but a 1px off-screen native
// EDIT control owns keyboard focus so dialog navigation/host shortcuts cannot
// starve WM_CHAR / key events. The edit control never paints into the client.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gradation::picker {
inline constexpr LONG_PTR kPickerDlgCode =
    DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTTAB;
inline INT_PTR handlePickerGetDlgCode(HWND dialog) noexcept {
    SetWindowLongPtrW(dialog, DWLP_MSGRESULT, kPickerDlgCode);
    return TRUE;
}
inline LRESULT CALLBACK pickerKeyboardProxyWndProc(HWND control, UINT msg,
                                                    WPARAM wp, LPARAM lp) {
    if(msg==WM_GETDLGCODE) return kPickerDlgCode;
    switch(msg){
      case WM_CHAR: case WM_KEYDOWN: case WM_KEYUP:
      case WM_SYSCHAR: case WM_SYSKEYDOWN: case WM_SYSKEYUP: {
        const HWND parent=GetParent(control);
        if(parent) SendMessageW(parent,msg,wp,lp);
        return 0;
      }
      default:break;
    }
    const auto original=reinterpret_cast<WNDPROC>(
        GetWindowLongPtrW(control,GWLP_USERDATA));
    return original?CallWindowProcW(original,control,msg,wp,lp)
                   :DefWindowProcW(control,msg,wp,lp);
}
inline HWND createPickerKeyboardProxy(HWND dialog,HINSTANCE module) noexcept {
    // Visible to the window manager but entirely outside the parent's
    // clipping rect; no native text glyphs or default frames are displayed.
    HWND control=CreateWindowExW(0,L"EDIT",L"",
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
        -32,-32,1,1,dialog,nullptr,module,nullptr);
    if(!control)return nullptr;
    SetLastError(0);
    const LONG_PTR prior=SetWindowLongPtrW(control,GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(&pickerKeyboardProxyWndProc));
    if(!prior){
        DestroyWindow(control);
        return nullptr;
    }
    SetWindowLongPtrW(control,GWLP_USERDATA,prior);
    return control;
}
} // namespace gradation::picker
