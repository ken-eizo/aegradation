#pragma once
#include <memory>
#include <slugia/slugia.hpp>
namespace slugia { class VulkanRenderer; }
namespace gradation::picker {
struct PickerState;
// Strict Figma AOT: all visible controls, bounds, fonts, corners and
// strokes are the elements authored in ColorPicker.slugui itself.
// Only their DATA and existing selected-state shapes change at runtime.
class FigmaScene {
public:
 explicit FigmaScene(slugia::VectorAtlas& atlas);
 ~FigmaScene();
 FigmaScene(const FigmaScene&)=delete;
 FigmaScene& operator=(const FigmaScene&)=delete;
 // Returns true only if the source-native scene's appearance changed.
 bool update(const PickerState& state);
 // Renderer-owned 32f images replace 96x6 antialiased gradient bands.
 bool uploadRaster(slugia::VulkanRenderer& renderer, const PickerState& state);
 void markSubmitted(std::uint64_t serial) noexcept;
 // A new Vulkan device never inherits renderer-local image IDs. This
 // preserves CPU font/geometry caches safely across modal openings.
 void resetRendererResources() noexcept;
 void render(const slugia::InputState& input,
             slugia::DrawList& draw,slugia::Rect viewport,float scale);
private:
 struct Impl;
 std::unique_ptr<Impl> impl_;
};
} // namespace gradation::picker
