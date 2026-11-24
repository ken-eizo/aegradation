#pragma once
#include <cstdint>
#include "picker_axes.hpp"
namespace gradation::picker {
struct PickerPalette {
 std::uint32_t background, text, normal, hover, pressed, disabled,
    border, field, fieldText, focus, number, selection, selectedText,
    checkMark, hoverSurface, pressedSurface;
};
inline constexpr PickerPalette pickerPalette(int theme) noexcept {
 if(theme==2)return {0xf6f6f6,0x505050,0x6d6d6d,0x464646,0x252525,0xa8a8a8,
  0xd3d3d3,0xffffff,0x323232,0x1478f8,0x1478f8,0x0078d7,0xffffff,
  0xffffff,0xe8e8e8,0xd9d9d9};
 if(theme==1)return {0x323232,0xc9c9c9,0xb0b0b0,0xd0d0d0,0xebebeb,0x777777,
  0x555555,0x222222,0xe8e8e8,0x4096f3,0x4095f2,0x0078d7,0xffffff,
  0x323232,0x424242,0x505050};
 return {0x1d1d1d,0xb0b0b0,0xb0b0b0,0xd0d0d0,0xebebeb,0x6a6a6a,
  0x4b4b4b,0x0e0e0e,0xe8e8e8,0x4096f3,0x4095f2,0x0078d7,0xffffff,
  0x1d1d1d,0x303030,0x424242};
}
inline constexpr std::uint32_t controlTint(PickerPalette p,WidgetState s) noexcept {
 switch(s){case WidgetState::Hover:case WidgetState::Focus:return p.hover;
 case WidgetState::Pressed:return p.pressed;
 case WidgetState::Disabled:return p.disabled;default:return p.normal;}
}
}
