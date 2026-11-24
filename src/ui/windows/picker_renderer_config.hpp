#pragma once
// The Color Picker's H/S/B coordinates and authored Figma colors are
// display-encoded sRGB. Do not send them through an *_SRGB framebuffer,
// which would apply a second sRGB encoding to the 32f picker raster.
#include <slugia/vulkan_renderer.hpp>

namespace gradation::picker {
[[nodiscard]] inline slugia::RendererConfig pickerRendererConfig() noexcept {
    auto config = slugia::RendererConfig::lowSpec();
    config.validation = false;
    config.vsync = true;
    // Requests VK_FORMAT_*_UNORM with VK_COLOR_SPACE_SRGB_NONLINEAR_KHR.
    // Slugia's ordinary clients keep their default linear-light setting.
    config.preferSrgbAlphaCompositing = true;
    config.externalImagesAreSrgb = true;
    config.enableInProcessPipelineCache = true;
    config.maxOutOfDateRetries = 2;
    config.clearColor = slugia::Color::fromRgb8(0x1d1d1d);
    return config;
}
} // namespace gradation::picker
