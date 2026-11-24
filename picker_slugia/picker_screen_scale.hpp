#pragma once
// Physical screen-fraction scaling of the 797x444 Figma viewport.
// Reference: 3840x2160 workspace (4K). FHD gets half the physical
// size, which occupies the SAME fraction of the screen.
#include <algorithm>
#include <cmath>

namespace gradation::picker {
inline constexpr int kDesignedWidth=797, kDesignedHeight=444;
struct PickerOuterSize{int width,height;};
// The standard AE SV plane is 386px at 150% and 257px at 100%.
 // The authored 797x444 client includes the extra alpha bar. Size the CLIENT,
 // not the outer window: the OS owns title bar/frame metrics independently.
[[nodiscard]] inline PickerOuterSize pickerClientSize(unsigned dpi) noexcept {
 const float scale=float(dpi?dpi:96)/144.f;
 return {std::max(1,int(std::lround(kDesignedWidth*scale))),
         std::max(1,int(std::lround(kDesignedHeight*scale)))};
}
[[nodiscard]] inline float pickerClientScale(int width,int height) noexcept {
 if(width<=0||height<=0)return 1.f;
 return std::min(float(width)/kDesignedWidth,float(height)/kDesignedHeight);
}
[[nodiscard]] inline float pickerScreenScale(int workWidth,int workHeight) noexcept {
 if(workWidth<=0||workHeight<=0)return 1.f;
 return std::clamp(std::min(float(workWidth)/3840.f,
                            float(workHeight)/2160.f),.25f,4.f);
}
[[nodiscard]] inline int pickerPhysicalWidth(float k) noexcept {
 return std::max(1,int(std::lround(kDesignedWidth*k)));
}
[[nodiscard]] inline int pickerPhysicalHeight(float k) noexcept {
 return std::max(1,int(std::lround(kDesignedHeight*k)));
}
}
