#pragma once
#include <slugia/draw_list.hpp>

namespace gradation::picker {
// The Figma SlugUI runtime emits ProjectedDrawListCommand for authored
// transforms. Slugia supports these in frame draw(), but explicitly
// rejects them in create/updateRetainedDrawList().
// Keep the AEX and headless regression test on this same submission path.
inline bool needsPickerFrame(bool sceneChanged, bool forcedRepaint,
                             slugia::Vec2 framebuffer, slugia::Vec2 lastFrame) noexcept {
  return forcedRepaint || sceneChanged ||
         framebuffer.x != lastFrame.x || framebuffer.y != lastFrame.y;
}

template<class Renderer>
void submitPickerDrawList(Renderer& renderer, const slugia::DrawList& list) {
  renderer.draw(list);
}
} // namespace gradation::picker
