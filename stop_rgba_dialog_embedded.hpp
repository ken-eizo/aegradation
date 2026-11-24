#pragma once
// In-process SlugUI color dialog, hosted by native Windows DialogBoxIndirectParamW.
// The AE EffectsPlugin has no standalone EXE or external UI process dependency.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "stop_color_picker_model.hpp"
#include "stop_value_codec.hpp"
#include <string>
namespace gradation::stopui {
// false = Cancel (failureReason empty), or initialization failure
// (failureReason receives the detail for AE's SDK error reporting).
using ColorPreviewCallback = void(*)(void*,const RGBA&) noexcept;
// Optional UI-thread preview callback: every accepted color change and the
// final OK/Cancel transition are forwarded without creating AE worker threads.
#if defined(_WIN32)
using NativeOwner=HWND;
#elif defined(__APPLE__)
using NativeOwner=void*;
NativeOwner nativeOwner(const char** failure) noexcept;
#endif
bool editRGBA(NativeOwner owner,RGBA& value,Depth depth,
              std::string* failureReason=nullptr,
              ColorPreviewCallback live=nullptr,void* context=nullptr,
              int themeIndex=0) noexcept;
}
