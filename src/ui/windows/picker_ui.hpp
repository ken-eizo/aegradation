#pragma once
#include "../common/stop_color_picker_model.hpp"
#include "../common/stop_value_codec.hpp"
#include "../common/picker_axes.hpp"
#include <slugia/slugia.hpp>
#include <array>
#include <string>
namespace gradation::picker {
struct PickerState {
 stopui::RGBA initial{1.f,.869f,.251f,1.f};
 stopui::RGBA current=initial;
 stopui::HSV hsv=stopui::toHSV(current);
 stopui::Depth depth=stopui::Depth::Bpc32;
 // AE appearance themes: 0 darkest (#1d), 1 gray (#32), 2 light (#f8).
 // Presentation only, never part of the serialized RGBA values.
 int theme=0;
 const slugia::VectorAtlas* metricsAtlas=nullptr;
 AxisMode axis=AxisMode::Hue;
 // Visual states come from the source Button-State.slugui Spectrum
 // variants. These never alter the Figma-authored control bounds.
 std::array<WidgetState,7> radioStates{};
 WidgetState okState=WidgetState::Normal;
 WidgetState cancelState=WidgetState::Normal;
 WidgetState previewState=WidgetState::Normal;
 WidgetState pipetteState=WidgetState::Normal;
 bool hexFocused=false;
 // Selection/caret are read-only from Slugia's text editor. The
 // Figma Button-State visual primitives render them; not UiContext.
 std::size_t hexCaret=0;
 std::size_t hexSelectionBegin=0;
 std::size_t hexSelectionEnd=0;
 std::array<std::string,8> fields{};
 bool preview=true;
 bool eyedropper=false;
 bool accepted=false;
 bool finished=false;
 bool invalidDraft=false;
 bool caretVisible=true;
 int numericFocus=-1;
 std::array<std::size_t,7> numericCaret{};
 std::array<std::size_t,7> numericSelectionBegin{};
 std::array<std::size_t,7> numericSelectionEnd{};
 int scrubPending=-1;
 int scrubIndex=-1;
 bool scrubActive=false;
 float scrubStartX=0.f, scrubInitialValue=0.f;
 void sync(slugia::WidgetId focused=0);
 bool updateField(int index);
};
// Input handling ONLY: draw target is deliberately discarded, so there
// are zero visible hand-authored UI elements outside ColorPicker.slugui.
void drawPicker(slugia::DrawList& discarded,slugia::UiContext& ui,
                const slugia::InputState& input,PickerState& state,float scale);
} // namespace gradation::picker
