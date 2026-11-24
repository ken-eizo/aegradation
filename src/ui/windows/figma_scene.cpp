// Figma is the authority for ALL fixed UI layout and all default visuals.
// Canonical design: picker_slugia/ColorPicker.slugui (stroke-v6).
// State palette / focus dimensions: picker_slugia/Button-State.slugui.
// Neither source file is rewritten, scaled locally, or manually re-exported.
#include "figma_scene.hpp"
#include "picker_ui.hpp"
#include "picker_raster.hpp"
#include "picker_text_metrics.hpp"
#include "picker_theme.hpp"
#include <slugia/vulkan_interop.hpp>
#include <slugia/vulkan_renderer.hpp>
#include <span>
#include <stdexcept>
#include "color_picker.generated.hpp"
#include "button_state.generated.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unordered_map>

namespace gradation::picker {
namespace su=slugia;
namespace sg=slugia::slugui;
namespace {
sg::Element* findNode(sg::Element& node,std::string_view id){
 if(node.name==id)return &node;
 for(auto& child:node.children)
  if(auto* match=findNode(child,id))return match;
 return nullptr;
}
su::Paint solid(uint32_t rgb){
 return su::Paint::solid(su::Color::fromRgb8(rgb));
}
su::Paint clear(){
 return su::Paint::solid({0.f,0.f,0.f,0.f});
}
su::Color cRGBA(stopui::RGBA c){
 return {stopui::uiClamp(c.r),stopui::uiClamp(c.g),
         stopui::uiClamp(c.b),stopui::uiClamp(c.a)};
}
void setText(sg::Element& root,std::string_view name,const std::string& value){
 if(auto* node=findNode(root,name))
  if(auto* text=std::get_if<sg::TextVisual>(&node->visual)){
   text->text=value;
   text->runs.clear(); // invalidate source sample glyph runs, NOT style.
  }
}
void setPaint(sg::Element& root,std::string_view name,su::Paint value){
 if(auto* node=findNode(root,name))
  if(auto* rect=std::get_if<sg::RoundedRectangleVisual>(&node->visual))
   rect->paint.normal=std::move(value);
}
void setLabelColor(sg::Element& root,std::string_view id,uint32_t rgb){
 if(auto* node=findNode(root,id))
  if(auto* value=std::get_if<sg::TextVisual>(&node->visual))
   value->paint.normal=solid(rgb);
}
bool same(const PickerState& a,const PickerState& b){
 const auto& x=a.current;const auto& y=b.current;
 return x.r==y.r&&x.g==y.g&&x.b==y.b&&x.a==y.a&&
        a.initial.r==b.initial.r&&a.initial.g==b.initial.g&&
        a.initial.b==b.initial.b&&a.initial.a==b.initial.a&&
        a.hsv.h==b.hsv.h&&a.hsv.s==b.hsv.s&&a.hsv.v==b.hsv.v&&
        a.axis==b.axis&&a.theme==b.theme&&a.preview==b.preview&&
        a.invalidDraft==b.invalidDraft&&a.hexFocused==b.hexFocused&&
        a.hexCaret==b.hexCaret&&
        a.hexSelectionBegin==b.hexSelectionBegin&&
        a.hexSelectionEnd==b.hexSelectionEnd&&
        a.radioStates==b.radioStates&&
        a.okState==b.okState&&a.cancelState==b.cancelState&&
        a.previewState==b.previewState&&
        a.pipetteState==b.pipetteState&&a.eyedropper==b.eyedropper&&
        a.fields==b.fields&&
        a.caretVisible==b.caretVisible&&
        a.numericFocus==b.numericFocus&&
        a.scrubActive==b.scrubActive&&a.scrubIndex==b.scrubIndex&&
        a.numericCaret==b.numericCaret&&
        a.numericSelectionBegin==b.numericSelectionBegin&&
        a.numericSelectionEnd==b.numericSelectionEnd;
}
// Exact Spectrum colors in Button-State.slugui:
// Default #B0B0B0, Hover/Focus #D0D0D0, Touch #EBEBEB,
// Disabled #6A6A6A, focus outline #4096F3, edit selection #0078D7.
// The author's ColorPicker palette takes precedence in the normal state.
uint32_t stateStroke(WidgetState s){
 switch(s){
  case WidgetState::Hover:
  case WidgetState::Focus:return 0xd0d0d0;
  case WidgetState::Pressed:return 0xebebeb;
  case WidgetState::Disabled:return 0x6a6a6a;
  default:return 0xb0b0b0;
 }
}
uint32_t stateLabel(WidgetState s){
 if(s==WidgetState::Disabled)return 0x6a6a6a;
 if(s!=WidgetState::Normal)return 0xffffff;
 return 0xa6a6a6; // original ColorPicker label, not a replacement.
}
sg::Element focusRing(std::string_view name,float x,float y,
                      float w,float h,float radius){
 auto node=sg::roundedRectangle(
   su::hashId(name),clear(),su::CornerRadii::all(radius),
   su::CornerSmoothing{},std::string(name));
 node.layout.x=sg::Length::logical(x);
 node.layout.y=sg::Length::logical(y);
 node.layout.width=sg::Length::logical(w);
 node.layout.height=sg::Length::logical(h);
 auto& rect=std::get<sg::RoundedRectangleVisual>(node.visual);
 rect.stroke.paint.normal=solid(0x4096f3); // Button-State.focus
 rect.stroke.width=2.f;
 rect.stroke.align=su::StrokeAlign::Center;
 node.visible=false;
 return node;
}
// An overlay stroke is needed because SlugUI renders the authored parent
// frame *before* its image child; a parent-only stroke would be hidden under
// the opaque raster. It is not an antialiased stack of edge rectangles.
sg::Element preciseInnerBorder(std::string_view label,float w,float h){
 auto node=sg::roundedRectangle(su::hashId(label),clear(),
                                su::CornerRadii::all(0.f),
                                su::CornerSmoothing{},std::string(label));
 node.layout.x=sg::Length::logical(0.f);
 node.layout.y=sg::Length::logical(0.f);
 node.layout.width=sg::Length::logical(w);
 node.layout.height=sg::Length::logical(h);
 auto& v=std::get<sg::RoundedRectangleVisual>(node.visual);
 v.stroke.paint.normal=solid(0x4b4b4b);
 v.stroke.width=1.f;
 v.stroke.align=su::StrokeAlign::Inside;
 return node;
}
sg::Element contentBand(int id,float x,float y,float w,float h){
 auto node=sg::roundedRectangle(
   su::hashId(std::string("gradation.dynamic-color.")+std::to_string(id)),
   solid(0x000000),su::CornerRadii::all(0.f),su::CornerSmoothing{},
   std::string("ColorData_")+std::to_string(id));
 node.layout.x=sg::Length::logical(x);
 node.layout.y=sg::Length::logical(y);
 node.layout.width=sg::Length::logical(w);
 node.layout.height=sg::Length::logical(h);
 node.visible=false;
 return node;
}
} // anonymous namespace

struct FigmaScene::Impl{
 // Source Figma frames: 388px square and 29px slider, both with
 // 1px INSIDE strokes. Raster exactly covers the remaining interior.
 static constexpr int imageSize=386,sliderWidth=27;
 gradation::figma::ColorPickerGenerated generated;
 gradation::figma::ButtonStateGenerated buttonReference;
 sg::Runtime runtime;
 // Scene node addresses remain stable after the constructor's final
 // ensureImages(). Fast O(1) lookup on hover, typing and caret blink avoids
 // dozens of full-tree walks through the Figma-authored scene.
 std::unordered_map<std::string_view,sg::Element*> index{};
 sg::Element* find(std::string_view name) const noexcept {
  const auto it=index.find(name);
  return it==index.end()?nullptr:it->second;
 }
 void indexNodes() {
  index.clear();
  auto visit=[&](auto& self,sg::Element& item)->void{
   index.emplace(item.name,&item);
   for(auto& child:item.children)self(self,child);
  };
  visit(visit,generated.component.root());
 }
 su::VectorAtlas& fontAtlas;
 sg::Visual selectedRadio{},unselectedRadio{};
 sg::RoundedRectangleVisual originalFrame{};
 sg::RoundedRectangleVisual originalSlider{};
 sg::RoundedRectangleVisual originalOk{},originalCancel{};
 sg::RoundedRectangleVisual originalPreview{},originalHex{};
 sg::Element* planeImageNode=nullptr;
 sg::Element* sliderImageNode=nullptr;
 bool imagesBuilt=false;
 static constexpr size_t imageBuffers=3;
 std::array<su::ExternalImageId,imageBuffers> planeImages{},sliderImages{},alphaImages{},previewImages{};
 std::array<std::uint64_t,imageBuffers> inUseSerial{};
 size_t imageSlot=0;
 su::ExternalImageId planeImageId=0,sliderImageId=0,alphaImageId=0;
 sg::Element* alphaImageNode=nullptr;
 sg::Element* previewImageNode=nullptr;
 su::ExternalImageId previewImageId=0;
 std::vector<float> pixels{};
 // Each of the three GPU buffer slots remembers which portions of its
 // four textures are valid. Reuse untouched images without CPU rasterization
 // or GPU uploads, rather than repainting all four on every value change.
 std::array<PickerState,imageBuffers> slotPrevious{};
 std::array<std::array<bool,4>,imageBuffers> slotValid{};
 PickerState rasterPrevious{};
 bool rasterValid=false;
 PickerState previous{};
 bool hasPrevious=false;

 explicit Impl(su::VectorAtlas& atlas):
   generated(atlas),buttonReference(atlas),fontAtlas(atlas){
  auto& root=generated.component.root();
  // Latest full 797x444 design is compiled directly (no duplicate subtree).
  // Refer to the actual Figma Shape's stroke-winding paths, rather than
  // substituting a hand-drawn checkbox/radio ellipse.
  if(auto* node=findNode(root,"Radio_2_247"))selectedRadio=node->visual;
  if(auto* node=findNode(root,"Radio_2_253"))unselectedRadio=node->visual;
  if(auto* node=findNode(root,"Color_Frame_1_43"))
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&node->visual))
    originalFrame=*v;
  if(auto* node=findNode(root,"Color_Slider_1_55"))
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&node->visual))
    originalSlider=*v;
  if(auto* node=findNode(root,"Button_Desktop_Darkest_2_814"))
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&node->visual))
    originalOk=*v;
  if(auto* node=findNode(root,"Button_Desktop_Darkest_2_816"))
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&node->visual))
    originalCancel=*v;
  if(auto* node=findNode(root,"Checkbox_Selected_2_987"))
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&node->visual))
    originalPreview=*v;
  if(auto* node=findNode(root,"Rectangle_2_2_1005"))
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&node->visual))
    originalHex=*v;

  constexpr std::array<std::string_view,7> groups{
    "Group_6_2_246","Group_7_2_252","Group_8_2_257",
    "Group_9_2_262","Group_10_2_267","Group_11_2_271",
    "Group_12_2_818"};
  // Figma Button-State focus outline is 4px outside the 14px box:
  // extrapolate its native outer offset only, retain the ColorPicker's
  // exact authored 18px radio and 24px row bounds.
  for(size_t i=0;i<groups.size();++i){
   if(auto* group=findNode(root,groups[i]))
    group->children.push_back(focusRing(
       std::string("FocusRadio_")+std::to_string(i),
       -4.f,-1.f,26.f,26.f,13.f));
  }
  if(auto* group=findNode(root,"Group_13_2_990"))
   group->children.push_back(focusRing(
        "FocusPreview",-4.f,-4.f,26.f,26.f,6.f));
  if(auto* window=findNode(root,"Color_Picker_1_42")){
   window->children.push_back(focusRing(
      "FocusOk",671.f,26.f,105.f,40.f,20.f));
   window->children.push_back(focusRing(
      "FocusCancel",671.f,79.f,105.f,40.f,20.f));
  }
  constexpr std::array<float,7> numericY{145.f,177.f,209.f,249.f,281.f,313.f,345.f};
  if(auto* window=findNode(root,"Color_Picker_1_42")){
   for(int i=0;i<7;++i){
    const auto name=std::to_string(i);
    // All edit layers share the value text's local group coordinates.
    // The focus box is BELOW the selection and the original source glyphs.
    auto focus=focusRing("NumericFocus_"+name,48.f,-4.f,72.f,32.f,4.f);
    if(auto* group=findNode(root,groups[(size_t)i]))
     group->children.insert(group->children.begin(),std::move(focus));
    auto select=sg::roundedRectangle(
       su::hashId("gradation.numeric.selection."+name),
       solid(0x0078d7),su::CornerRadii::all(0.f),su::CornerSmoothing{},
       "NumericSelection_"+name);
    // Local group coordinates: (56,3), behind the source numeric glyphs.
    select.layout.x=sg::Length::logical(57.f);
    select.layout.y=sg::Length::logical(3.f);
    select.layout.width=sg::Length::logical(8.f);
    select.layout.height=sg::Length::logical(18.f);
    select.visible=false;
    // Insert BEFORE the authored numeric text, so selected digits remain
    // legible (white on blue) while the separate %/° source node is untouched.
    if(auto* group=findNode(root,groups[(size_t)i]))
     group->children.insert(group->children.begin()+1,std::move(select));
    auto caret=sg::roundedRectangle(
       su::hashId("gradation.numeric.caret."+name),
       solid(0x4096f3),su::CornerRadii::all(0.f),su::CornerSmoothing{},
       "NumericCaret_"+name);
    caret.layout.x=sg::Length::logical(57.f);
    caret.layout.y=sg::Length::logical(3.f);
    caret.layout.width=sg::Length::logical(1.f);
    caret.layout.height=sg::Length::logical(18.f);
    caret.visible=false;
    if(auto* group=findNode(root,groups[(size_t)i]))
     group->children.push_back(std::move(caret));
   }
  }
  // Focus/hover backgrounds sit BEHIND the exact authored Figma SVG.
  // The source icon is not redrawn or replaced with an approximation.
  if(auto* window=findNode(root,"Color_Picker_1_42")){
   auto button=sg::roundedRectangle(
     su::hashId("gradation.pipette.background"),clear(),
     su::CornerRadii::all(6.f),su::CornerSmoothing{},"PipetteBackground");
   button.layout.x=sg::Length::logical(687.f);
   button.layout.y=sg::Length::logical(165.f);
   button.layout.width=sg::Length::logical(32.f);
   button.layout.height=sg::Length::logical(32.f);
   button.visible=false;
   const auto pos=std::find_if(window->children.begin(),window->children.end(),
      [](const sg::Element& e){return e.name=="Group_14_2_1004";});
   window->children.insert(pos,std::move(button));
  }
  // Import EXACT source Figma selection/caret primitives from
  // Button-State.slugui. The reference group's 82x32 dimensions are
  // identical to the new ColorPicker HEX field at 571,379.
  if(auto* hex=findNode(root,"Rectangle_2_2_1005")){
   auto& reference=buttonReference.component.root();
   if(auto* selection=findNode(reference,"Rectangle_5_31_1061")){
    sg::Element visual=*selection;
    visual.name="HexSelectionOverlay";
    visual.id=su::hashId("gradation.hex.selection");
    visual.visible=false;
    hex->children.push_back(std::move(visual));
   }
   auto caret=sg::roundedRectangle(
    su::hashId("gradation.hex.caret"),solid(0xffffff),
    su::CornerRadii::all(0.f),su::CornerSmoothing{},"HexCaret");
   caret.layout.x=sg::Length::logical(13.f);
   caret.layout.y=sg::Length::logical(7.f);
   caret.layout.width=sg::Length::logical(1.f);
   caret.layout.height=sg::Length::logical(18.f);
   caret.visible=false;
   hex->children.push_back(std::move(caret));
  }
  // Finish every mutable tree insertion before caching StringView keys.
  ensureImages();
  indexNodes();
 }
 // Only modes other than Hue need generated pixel data. Hue's original
 // Figma fill-layers and 13-stop Hue slider stay unmodified on startup.
 void ensureImages(){
  if(imagesBuilt)return;
  auto& root=generated.component.root();
  if(auto* frame=findNode(root,"Color_Frame_1_43")){
   auto raster=sg::absolute(su::hashId("gradation.plane.raster"),"PlaneRaster");
   raster.layout.x=sg::Length::logical(1.f);
   raster.layout.y=sg::Length::logical(1.f);
   raster.layout.width=sg::Length::logical(float(imageSize));
   raster.layout.height=sg::Length::logical(float(imageSize));
   raster.visual=sg::ExternalImageVisual{planeImageId,imageSize,imageSize};
   raster.visible=false;
   frame->children.insert(frame->children.begin(),std::move(raster));
   frame->children.insert(frame->children.begin()+1,
      preciseInnerBorder("GradientPlaneInnerBorder",388.f,388.f));
  }
  if(auto* slider=findNode(root,"Color_Slider_1_55")){
   auto raster=sg::absolute(su::hashId("gradation.slider.raster"),"SliderRaster");
   raster.layout.x=sg::Length::logical(1.f);
   raster.layout.y=sg::Length::logical(1.f);
   raster.layout.width=sg::Length::logical(float(sliderWidth));
   raster.layout.height=sg::Length::logical(float(imageSize));
   raster.visual=sg::ExternalImageVisual{sliderImageId,sliderWidth,imageSize};
   raster.visible=false;
   slider->children.insert(slider->children.begin(),std::move(raster));
   slider->children.push_back(preciseInnerBorder("GradientSliderInnerBorder",29.f,388.f));
   slider->clipsChildren=true;
  }
  if(auto* alpha=findNode(root,"Transparency_1_62")){
   auto raster=sg::absolute(su::hashId("gradation.alpha.raster"),"AlphaRaster");
   raster.layout.x=sg::Length::logical(1.f);
   raster.layout.y=sg::Length::logical(1.f);
   raster.layout.width=sg::Length::logical(float(sliderWidth));
   raster.layout.height=sg::Length::logical(float(imageSize));
   raster.visual=sg::ExternalImageVisual{0,sliderWidth,imageSize};
   raster.visible=false;
   // Rebuild the Figma checker pixel-perfectly inside the numerical
   // AlphaRaster, rather than traversing hundreds of baked vector shapes.
   // The original .slugui files stay byte-identical.
   alpha->children.clear();
   alpha->children.push_back(std::move(raster));
   alpha->children.push_back(preciseInnerBorder("GradientAlphaInnerBorder",29.f,388.f));
   alpha->clipsChildren=true;
  }
  if(auto* preview=findNode(root,"Rectangle_1_1_169")){
   auto image=sg::absolute(su::hashId("gradation.preview.checker"),"PreviewColorRaster");
   image.layout.x=sg::Length::logical(0.f);
   image.layout.y=sg::Length::logical(0.f);
   image.layout.width=sg::Length::logical(86.019f);
   image.layout.height=sg::Length::logical(48.5f);
   image.visual=sg::ExternalImageVisual{0,172,97};
   image.visible=false;
   preview->children.push_back(std::move(image));
  }
  // A SlugUI frame paints before its fully opaque children. Re-emit only
  // the uploaded #4B4B4B 1px INSIDE border last, otherwise top/bottom fills
  // obscure it. Geometry and color come from Frame_6_42_95 unchanged.
  if(auto* frame=findNode(root,"Frame_6_42_95")){
   if(auto* visual=std::get_if<sg::RoundedRectangleVisual>(&frame->visual))
    visual->stroke.width=0.f;
   frame->children.push_back(preciseInnerBorder(
      "PreviewInsideBorder",86.019f,97.f));
  }
  planeImageNode=findNode(root,"PlaneRaster");
  sliderImageNode=findNode(root,"SliderRaster");
  alphaImageNode=findNode(root,"AlphaRaster");
  previewImageNode=findNode(root,"PreviewColorRaster");
  if(!planeImageNode||!sliderImageNode||!alphaImageNode||!previewImageNode)
   throw std::runtime_error("ColorPicker: authored raster node missing");
  imagesBuilt=true;
 }
 void updateColors(const PickerState& state){
  auto& root=generated.component.root();
  ensureImages();
  if(auto* frame=findNode(root,"Color_Frame_1_43"))
   if(auto* r=std::get_if<sg::RoundedRectangleVisual>(&frame->visual)){
    *r=originalFrame;
    r->fillLayers.clear();
    r->radii=su::CornerRadii::all(0.f);
    r->paint.normal=clear();
    r->stroke.width=0.f; // Overlay: 1px #4B4B4B fully opaque
   }
  if(auto* slider=findNode(root,"Color_Slider_1_55"))
   if(auto* r=std::get_if<sg::RoundedRectangleVisual>(&slider->visual)){
    *r=originalSlider;
    r->radii=su::CornerRadii::all(0.f);
    r->paint.normal=clear();
    r->stroke.width=0.f; // Overlay: 1px #4B4B4B fully opaque
   }
  if(auto* alpha=findNode(root,"Transparency_1_62"))
   if(auto* r=std::get_if<sg::RoundedRectangleVisual>(&alpha->visual)){
    r->radii=su::CornerRadii::all(0.f);
    r->stroke.width=0.f; // Overlay: 1px #4B4B4B fully opaque
   }
  if(planeImageNode)planeImageNode->visible=planeImageId!=0;
  if(sliderImageNode)sliderImageNode->visible=sliderImageId!=0;
  if(alphaImageNode)alphaImageNode->visible=alphaImageId!=0;
  if(previewImageNode)previewImageNode->visible=previewImageId!=0;
  if(auto* gradient=findNode(root,"Gradient_Overlay_1_64"))
   gradient->visible=alphaImageId==0;
 }
};

FigmaScene::FigmaScene(su::VectorAtlas& atlas):impl_(std::make_unique<Impl>(atlas)){}
FigmaScene::~FigmaScene()=default;

bool FigmaScene::update(const PickerState& state){
 if(impl_->hasPrevious&&same(impl_->previous,state))return false;
 // A hover/focus transition changes only paint state of a few controls.
 // Never rebuild 576 gradient bands (or the authored 3-fill hue square)
 // for a hover, text caret, checkbox focus, or unchanged color value.
 const bool colorChanged=!impl_->hasPrevious||
   impl_->previous.axis!=state.axis||
   impl_->previous.hsv.h!=state.hsv.h||
   impl_->previous.hsv.s!=state.hsv.s||
   impl_->previous.hsv.v!=state.hsv.v||
   impl_->previous.current.r!=state.current.r||
   impl_->previous.current.g!=state.current.g||
   impl_->previous.current.b!=state.current.b||
   impl_->previous.current.a!=state.current.a;
 const bool hadPrevious=impl_->hasPrevious;
 const auto& old=impl_->previous;
 const bool themeChanged=!hadPrevious||old.theme!=state.theme;
 const bool axisChanged=!hadPrevious||old.axis!=state.axis;
 const bool numericFocusChanged=!hadPrevious||old.numericFocus!=state.numericFocus;
 std::array<bool,7> radioChanged{};
 for(size_t i=0;i<radioChanged.size();++i)
  radioChanged[i]=axisChanged||themeChanged||old.radioStates[i]!=state.radioStates[i];
 const bool previewVisualChanged=!hadPrevious||
   themeChanged||old.preview!=state.preview||old.previewState!=state.previewState;
 const bool previewColorChanged=colorChanged||!hadPrevious||
   old.preview!=state.preview||
   old.initial.r!=state.initial.r||old.initial.g!=state.initial.g||
   old.initial.b!=state.initial.b||old.initial.a!=state.initial.a;
 const bool pipetteVisualChanged=!hadPrevious||
   themeChanged||old.pipetteState!=state.pipetteState||old.eyedropper!=state.eyedropper;
 const bool numericVisualChanged=!hadPrevious||themeChanged||
   old.numericFocus!=state.numericFocus||
   old.numericCaret!=state.numericCaret||
   old.numericSelectionBegin!=state.numericSelectionBegin||
   old.numericSelectionEnd!=state.numericSelectionEnd||
   old.scrubActive!=state.scrubActive||old.scrubIndex!=state.scrubIndex||
   old.caretVisible!=state.caretVisible||
   old.fields!=state.fields;
 const bool hexVisualChanged=!hadPrevious||themeChanged||
   old.hexFocused!=state.hexFocused||
   old.hexCaret!=state.hexCaret||
   old.hexSelectionBegin!=state.hexSelectionBegin||
   old.hexSelectionEnd!=state.hexSelectionEnd||
   old.caretVisible!=state.caretVisible||old.fields[7]!=state.fields[7];
 const std::array<bool,2> buttonVisualChanged{
   !hadPrevious||themeChanged||old.okState!=state.okState,
   !hadPrevious||themeChanged||old.cancelState!=state.cancelState};
 std::array<bool,8> changedText{};
 for(size_t i=0;i<changedText.size();++i)
  changedText[i]=!hadPrevious||old.fields[i]!=state.fields[i];
 impl_->previous=state;
 impl_->hasPrevious=true;
 auto& root=impl_->generated.component.root();
 const auto palette=pickerPalette(state.theme);
 const auto advance=[&](std::string_view value,std::size_t count,bool hex=false){
  return pickerTextAdvance(&impl_->fontAtlas,value,count,hex);
 };
 const auto paintEdit=[&](sg::TextVisual& v,const std::string& value,
                          std::size_t begin,std::size_t end,bool focused,
                          std::uint32_t normal){
  const auto base=focused?palette.fieldText:normal;
  v.paint.normal=solid(base);
  v.runs.clear();
  begin=std::min(begin,value.size());end=std::min(end,value.size());
  if(!focused||end<=begin)return;
  const auto append=[&](std::size_t pos,std::size_t len,std::uint32_t color){
   if(!len)return;
   auto style=v.style;style.paint=solid(color);
   v.runs.push_back(su::TextRun{value.substr(pos,len),std::move(style)});
  };
  append(0,begin,base);
  append(begin,end-begin,palette.selectedText);
  append(end,value.size()-end,base);
 };
 if(!hadPrevious)impl_->updateColors(state); // static frame styling, not per-edit
 const auto setTextFast=[&](std::string_view name,const std::string& value){
  if(auto* node=impl_->find(name))
   if(auto* text=std::get_if<sg::TextVisual>(&node->visual)){
    text->text=value;
    text->runs.clear();
   }
 };
 const auto setPaintFast=[&](std::string_view name,su::Paint paint){
  if(auto* node=impl_->find(name))
   if(auto* rect=std::get_if<sg::RoundedRectangleVisual>(&node->visual))
    rect->paint.normal=std::move(paint);
 };
 const auto setLabelFast=[&](std::string_view name,uint32_t color){
  if(auto* node=impl_->find(name))
   if(auto* text=std::get_if<sg::TextVisual>(&node->visual))
    text->paint.normal=solid(color);
 };

 // Only the source TEXT CONTENT changes, never its geometry or typography.
 constexpr std::array<std::string_view,8> labels{
  "Label_2_249","Label_2_255","Label_2_260",
  "Label_2_265","Label_2_270","Label_2_274",
  "Label_2_821","Label_2_1012"};
 for(size_t i=0;i<labels.size();++i)
  if(changedText[i])setTextFast(labels[i],state.fields[i]);
 // These are separate Figma text nodes; move ONLY their runtime suffix x
 // relative to the actual numeric glyph advances, never Figma source files.
 constexpr std::array<std::string_view,3> suffixIds{
   "Label_2_250","Label_2_256","Label_2_261"};
 for(size_t i=0;i<suffixIds.size();++i){
  if(!changedText[i]&&!numericFocusChanged)
   continue;
  const float width=advance(state.fields[i],state.fields[i].size());
  if(auto* suffix=impl_->find(suffixIds[i])){
   // AE suppresses % and degree signs during direct text editing. The
   // authored source text node itself remains unchanged.
   suffix->visible=state.numericFocus!=(int)i;
   suffix->layout.x=sg::Length::logical(57.f+width+3.f);
  }
  if(auto* value=impl_->find(labels[i]))
   value->layout.width=sg::Length::logical(std::max(19.f,width+2.f));
 }

 // Exact authored radio source paths for selected/unselected.
 constexpr std::array<std::string_view,7> radioIds{
  "Radio_2_247","Radio_2_253","Radio_2_258",
  "Radio_2_263","Radio_2_268","Radio_2_272",
  "Radio_2_819"};
 constexpr std::array<std::string_view,7> radioLabels{
  "Label_2_248","Label_2_254","Label_2_259",
  "Label_2_264","Label_2_269","Label_2_273",
  "Label_2_820"};
 for(size_t i=0;i<radioIds.size();++i){
  if(!radioChanged[i])continue; // no vector-copy/font/paint allocations
  const bool chosen=axisIndex(state.axis)==(int)i;
  const WidgetState currentState=state.radioStates[i];
  if(auto* node=impl_->find(radioIds[i])){
   node->visual=chosen?impl_->selectedRadio:impl_->unselectedRadio;
   if(auto* shape=std::get_if<sg::ShapeVisual>(&node->visual)){
    // Both source paths share a dark interior fill in the Figma export.
    // Repaint the fill as well as the ring; selected dot is source geometry.
    shape->paint.normal=solid(palette.background);
    const auto tint=controlTint(palette,currentState);
    shape->strokePaint.normal=solid(tint);
   }
  }
  if(auto* label=impl_->find(radioLabels[i]))
   if(auto* v=std::get_if<sg::TextVisual>(&label->visual)){
    v->style.weight=chosen?600:400;
    v->style.fontStyle=chosen?"SemiBold":"Regular";
    v->style.bold=false;
    v->paint.normal=solid(currentState==WidgetState::Normal?
                          palette.text:controlTint(palette,currentState));
   }
  if(auto* ring=impl_->find(std::string("FocusRadio_")+std::to_string(i))){
   ring->visible=currentState==WidgetState::Focus;
   if(auto* rect=std::get_if<sg::RoundedRectangleVisual>(&ring->visual))
    rect->stroke.paint.normal=solid(palette.focus);
  }
 }
 // The new design defines "Previous" TOP and "New" BOTTOM at unchanged
 // 94x53 Figma children, rather than a single 94x106 color rectangle.
 if(previewColorChanged){
  setPaintFast("Rectangle_3_10_6",su::Paint::solid(cRGBA(state.initial)));
  setPaintFast("Rectangle_1_1_169",su::Paint::solid(
    cRGBA(state.preview?state.current:state.initial)));
 }

 // 25x25 authored handle at (400,-12) with negative X transform.
 // Keep static geometry and text runs untouched on hover/caret updates.
 if(colorChanged){
 if(auto* handle=impl_->find("Handle_1_44")){
  const Coord pos=planePosition(state.axis,state.current,state.hsv);
  handle->layout.x=sg::Length::logical(400.f+388.f*(pos.x-1.f));
  handle->layout.y=sg::Length::logical(-12.f+388.f*pos.y);
 }
 if(auto* marker=impl_->find("Group_2_1_159"))
  marker->layout.y=sg::Length::logical(388.f*barY(state.axis,state.current,state.hsv));
 if(auto* marker=impl_->find("Group_3_1_160"))
  marker->layout.y=sg::Length::logical(388.f*(1.f-stopui::uiClamp(state.current.a)));
 }

 // Source checkbox icon remains untouched; palettes are derived from
 // Button-State.slugui's selected/unselected Spectrum variants.
 if(previewVisualChanged){
  if(auto* check=impl_->find("Vector_2_988")){
   check->visible=state.preview;
   if(auto* shape=std::get_if<sg::ShapeVisual>(&check->visual))
    shape->paint.normal=solid(palette.checkMark);
  }
  if(auto* box=impl_->find("Checkbox_Selected_2_987"))
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&box->visual)){
    *v=impl_->originalPreview;
    const auto tint=controlTint(palette,state.previewState);
    v->paint.normal=solid(state.preview?tint:palette.background);
    v->stroke.paint.normal=solid(tint);
    v->stroke.width=state.preview?0.f:2.f;
   }
  if(auto* ring=impl_->find("FocusPreview")){
   ring->visible=state.previewState==WidgetState::Focus;
   if(auto* rect=std::get_if<sg::RoundedRectangleVisual>(&ring->visual))
    rect->stroke.paint.normal=solid(palette.focus);
  }
  setLabelFast("Label_2_989",state.previewState==WidgetState::Normal?
    palette.text:controlTint(palette,state.previewState));
 }

 // Authored pipette SVG remains untouched. Only the behind-icon button
 // surface changes for Hover, Focus and Touch, matching the supplied states.
 if(pipetteVisualChanged)
 if(auto* bg=impl_->find("PipetteBackground")){
  const bool active=state.eyedropper;
  const bool show=state.pipetteState!=WidgetState::Normal||active;
  bg->visible=show;
  if(auto* rect=std::get_if<sg::RoundedRectangleVisual>(&bg->visual)){
   rect->paint.normal=solid(state.pipetteState==WidgetState::Pressed?
                             palette.pressedSurface:palette.hoverSurface);
   const bool ring=state.pipetteState==WidgetState::Focus||active;
   rect->stroke.width=ring?2.f:0.f;
   rect->stroke.align=su::StrokeAlign::Inside;
   rect->stroke.paint.normal=ring?solid(palette.focus):clear();
  }
 }
 // Both buttons retain their Figma position, shape, border, radii,
 // text and layout. Only paint state changes on user interaction.
 constexpr std::array<std::string_view,2> btnIds{
  "Button_Desktop_Darkest_2_814","Button_Desktop_Darkest_2_816"};
 constexpr std::array<std::string_view,2> txtIds{
  "Label_2_815","Label_2_817"};
 const std::array<WidgetState,2> btnStates{state.okState,state.cancelState};
 for(int i=0;i<2;++i){
  if(!buttonVisualChanged[(size_t)i])continue;
  const auto s=btnStates[(size_t)i];
  if(auto* node=impl_->find(btnIds[(size_t)i]))
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&node->visual)){
    *v=i==0?impl_->originalOk:impl_->originalCancel;
    if(s!=WidgetState::Normal){
     auto p=buttonColors(s,i==0);
     v->paint.normal=solid(p.fill);
     if(i==1){
      const auto& original=impl_->originalCancel.stroke.paint.normal.resolve(
          impl_->generated.component.properties());
      auto tint=su::Color::fromRgb8(p.outline);
      tint.a=original.start.a;
      v->stroke.paint.normal=su::Paint::solid(tint,original.opacity);
     }
    }
    if(i==1){
     v->paint.normal=solid(s==WidgetState::Pressed?palette.pressedSurface:
        s==WidgetState::Hover?palette.hoverSurface:palette.background);
     v->stroke.paint.normal=solid(s==WidgetState::Normal?
        palette.border:controlTint(palette,s));
    }
   }
  setLabelFast(txtIds[(size_t)i],i==0?buttonColors(s,true).text:
     s==WidgetState::Normal?palette.text:controlTint(palette,s));
 }
 for(const auto name:{"FocusOk","FocusCancel"})
  if(auto* ring=impl_->find(name)){
   ring->visible=(std::string_view(name)=="FocusOk"?state.okState:state.cancelState)
     ==WidgetState::Focus;
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&ring->visual))
    v->stroke.paint.normal=solid(palette.focus);
  }

 // Every editable value uses the same real glyph advance and local
 // origin for hit testing, selection, focus box and insertion caret.
 if(numericVisualChanged)
 for(int i=0;i<7;++i){
  const auto index=(size_t)i;
  const auto suffix=std::to_string(i);
  const auto& value=state.fields[index];
  const auto n=value.size();
  const auto begin=std::min(n,state.numericSelectionBegin[index]);
  const auto end=std::min(n,state.numericSelectionEnd[index]);
  const bool editing=state.numericFocus==i&&!state.scrubActive;
  const bool selected=editing&&end>begin;
  const float width=advance(value,n);
  if(auto* focus=impl_->find("NumericFocus_"+suffix)){
   focus->visible=editing;
   focus->layout.width=sg::Length::logical(std::max(72.f,width+18.f));
   if(auto* rect=std::get_if<sg::RoundedRectangleVisual>(&focus->visual)){
    rect->paint.normal=solid(palette.field);
    rect->stroke.paint.normal=solid(palette.focus);
    rect->stroke.width=2.5f;
   }
  }
  if(auto* selection=impl_->find("NumericSelection_"+suffix)){
   selection->visible=selected;
   selection->layout.x=sg::Length::logical(57.f+advance(value,begin));
   selection->layout.y=sg::Length::logical(3.f);
   selection->layout.width=sg::Length::logical(
    std::max(1.f,advance(value,end)-advance(value,begin)));
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&selection->visual))
    v->paint.normal=solid(palette.selection);
  }
  if(auto* caret=impl_->find("NumericCaret_"+suffix)){
   caret->visible=editing&&!selected&&state.caretVisible;
   caret->layout.x=sg::Length::logical(57.f+advance(value,
                        std::min(n,state.numericCaret[index])));
   caret->layout.y=sg::Length::logical(3.f);
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&caret->visual))
    v->paint.normal=solid(palette.fieldText);
  }
  if(auto* text=impl_->find(labels[index])){
   // Long 16bpc values and edited 32bpc drafts must not get clipped.
   text->layout.width=sg::Length::logical(std::max(19.f,width+2.f));
   if(auto* v=std::get_if<sg::TextVisual>(&text->visual)){
    v->style.underline=state.scrubActive&&state.scrubIndex==i;
    paintEdit(*v,value,begin,end,editing,palette.number);
   }
  }
 }
 if(hexVisualChanged){
  const auto& value=state.fields[7];
  const auto n=value.size();
  const auto begin=std::min(n,state.hexSelectionBegin);
  const auto end=std::min(n,state.hexSelectionEnd);
  const bool selected=state.hexFocused&&end>begin;
  if(auto* input=impl_->find("Rectangle_2_2_1005"))
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&input->visual)){
    *v=impl_->originalHex;
    v->paint.normal=solid(palette.field);
    v->stroke.paint.normal=solid(state.hexFocused?palette.focus:palette.border);
    v->stroke.width=state.hexFocused?2.5f:1.f;
   }
  if(auto* text=impl_->find("Label_2_1012")){
   text->layout.width=sg::Length::logical(std::max(51.f,advance(value,n,true)+2.f));
   if(auto* v=std::get_if<sg::TextVisual>(&text->visual))
    paintEdit(*v,value,begin,end,state.hexFocused,palette.fieldText);
  }
  if(auto* highlight=impl_->find("HexSelectionOverlay")){
   highlight->visible=selected;
   highlight->layout.x=sg::Length::logical(13.f+advance(value,begin,true));
   highlight->layout.y=sg::Length::logical(7.f);
   highlight->layout.width=sg::Length::logical(
      std::max(1.f,advance(value,end,true)-advance(value,begin,true)));
   highlight->layout.height=sg::Length::logical(18.f);
   if(auto* v=std::get_if<sg::RoundedRectangleVisual>(&highlight->visual))
    v->paint.normal=solid(palette.selection);
  }
  if(auto* caret=impl_->find("HexCaret")){
   caret->visible=state.hexFocused&&!selected&&state.caretVisible;
   caret->layout.x=sg::Length::logical(13.f+advance(value,
                    std::min(n,state.hexCaret),true));
   caret->layout.y=sg::Length::logical(7.f);
   if(auto* rect=std::get_if<sg::RoundedRectangleVisual>(&caret->visual))
    rect->paint.normal=solid(palette.fieldText);
  }
 }

 // The Figma geometry stays byte-identical. Only the native AE
 // appearance palette is changed at runtime, never the source assets.
 if(themeChanged){
  setPaintFast("Color_Picker_1_42",solid(palette.background));
  constexpr std::array<std::string_view,4> borders{
   "GradientPlaneInnerBorder","GradientSliderInnerBorder",
   "GradientAlphaInnerBorder","PreviewInsideBorder"};
  for(const auto name:borders)
   if(auto* node=impl_->find(name))
    if(auto* rect=std::get_if<sg::RoundedRectangleVisual>(&node->visual))
     rect->stroke.paint.normal=solid(palette.border);
  for(const auto name:{"Label_2_250","Label_2_256","Label_2_261","Label_2_1011"})
   setLabelFast(name,palette.text);
  // Preserve icon alpha, including intentionally empty fill/stroke.
  const auto tintShape=[&](auto& self,sg::Element& e)->void{
   if(auto* shape=std::get_if<sg::ShapeVisual>(&e.visual)){
    const auto tint=[&](auto& paint){
     const auto original=paint.normal.resolve(impl_->generated.component.properties());
     auto color=su::Color::fromRgb8(palette.normal);
     color.a=original.start.a;
     paint.normal=su::Paint::solid(color,original.opacity);
    };
    if(shape->shape)tint(shape->paint);
    if(shape->strokeShape)tint(shape->strokePaint);
   }
   for(auto& child:e.children)self(self,child);
  };
  for(const auto name:{"Group_14_2_1004","Group_2_1_159","Group_3_1_160"})
   if(auto* node=impl_->find(name))tintShape(tintShape,*node);
 }
 // The checker and Figma paths are unchanged; raster remains numerical.
 return true;
}

bool FigmaScene::uploadRaster(su::VulkanRenderer& renderer,const PickerState& state){
 auto& data=*impl_;
 data.ensureImages();
 const auto& prev=data.rasterPrevious;
 std::array<PickerRasterKey,4> keys{};
 bool sameRaster=data.rasterValid;
 for(int kind=0;kind<4;++kind){
  keys[(size_t)kind]=pickerRasterKey(state,kind);
  if(keys[(size_t)kind]!=pickerRasterKey(prev,kind))sameRaster=false;
 }
 if(sameRaster)return false;
 auto& interop=renderer.vulkanInterop();
 // Triple buffered mapped resources avoid an unconditional vkDeviceWaitIdle
 // on every click/drag; reuse only after the renderer confirmed completion.
 const auto findFree=[&](){
  const auto completed=interop.completedSubmissionSerial();
  for(size_t k=0;k<data.imageBuffers;++k){
   const size_t candidate=(data.imageSlot+1+k)%data.imageBuffers;
   if(data.inUseSerial[candidate]<=completed)return candidate;
  }
  return data.imageBuffers;
 };
 size_t slot=findFree();
 if(slot==data.imageBuffers){
  // Wait only for the renderer's next frame fence, which also advances
  // completedSubmissionSerial. Never idle the whole Vulkan device for
  // ordinary color edits. The caller must present the acquired frame.
  renderer.prepareFrame();
  slot=findFree();
  if(slot==data.imageBuffers)
   throw std::runtime_error("ColorPicker: no safe image slot after frame fence");
 }
 const auto& last=data.slotPrevious[slot];
 const auto& valid=data.slotValid[slot];
 const auto refresh=[&](int kind){
  return !valid[(size_t)kind]||keys[(size_t)kind]!=pickerRasterKey(last,kind);
 };
 const bool refreshPlane=refresh(0);
 const bool refreshHue=refresh(1);
 const bool refreshAlpha=refresh(2);
 const bool refreshPreview=refresh(3);
 const auto allocate=[&](su::ExternalImageId& id,int width,int height){
  if(id==0){
   id=interop.createOwnedRgba32fImage(width,height);
   if(!id)throw std::runtime_error("ColorPicker: external image allocation failed");
  }
 };
 allocate(data.planeImages[slot],data.imageSize,data.imageSize);
 allocate(data.sliderImages[slot],data.sliderWidth,data.imageSize);
 allocate(data.alphaImages[slot],data.sliderWidth,data.imageSize);
 allocate(data.previewImages[slot],172,97);
 const auto upload=[&](int width,int height,int kind,su::ExternalImageId id){
  data.pixels.resize(size_t(width)*height*4u);
  // Pixel-equivalent CPU generator: one HSV conversion per bar row
  // (instead of 27), constant preview/checker parameters outside loops.
  rasterizePickerImage(state,width,height,kind,std::span<float>(data.pixels));
  if(!interop.updateOwnedRgba32fImage(id,std::span<const float>(data.pixels)))
   throw std::runtime_error("ColorPicker: image upload failed");
 };
 if(refreshPlane)upload(data.imageSize,data.imageSize,0,data.planeImages[slot]);
 if(refreshHue)upload(data.sliderWidth,data.imageSize,1,data.sliderImages[slot]);
 if(refreshAlpha)upload(data.sliderWidth,data.imageSize,2,data.alphaImages[slot]);
 if(refreshPreview)upload(172,97,3,data.previewImages[slot]);
 data.slotPrevious[slot]=state;
 data.slotValid[slot].fill(true);
 data.imageSlot=slot;
 data.planeImageId=data.planeImages[slot];
 data.sliderImageId=data.sliderImages[slot];
 data.alphaImageId=data.alphaImages[slot];
 data.previewImageId=data.previewImages[slot];
 const auto bind=[](sg::Element* node,su::ExternalImageId id){
  if(!node)return;
  std::get<sg::ExternalImageVisual>(node->visual).image=id;
  node->visible=id!=0;
 };
 bind(data.planeImageNode,data.planeImageId);
 bind(data.sliderImageNode,data.sliderImageId);
 bind(data.alphaImageNode,data.alphaImageId);
 bind(data.previewImageNode,data.previewImageId);
 if(auto* overlay=findNode(data.generated.component.root(),"Gradient_Overlay_1_64"))
  overlay->visible=data.alphaImageId==0;
 data.rasterPrevious=state;
 data.rasterValid=true;
 return true;
}
void FigmaScene::markSubmitted(std::uint64_t serial) noexcept {
 auto& data=*impl_;
 if(data.rasterValid)data.inUseSerial[data.imageSlot]=serial;
}

void FigmaScene::resetRendererResources() noexcept{
 auto& data=*impl_;
 data.planeImages.fill(0);
 data.sliderImages.fill(0);
 data.alphaImages.fill(0);
 data.previewImages.fill(0);
 data.inUseSerial.fill(0);
 for(auto& valid:data.slotValid)valid.fill(false);
 data.planeImageId=data.sliderImageId=data.alphaImageId=0;
 data.previewImageId=0;
 data.imageSlot=0;
 data.rasterValid=false;
 data.hasPrevious=false;
 if(data.planeImageNode){
  data.planeImageNode->visible=false;
  std::get<sg::ExternalImageVisual>(data.planeImageNode->visual).image=0;
 }
 if(data.sliderImageNode){
  data.sliderImageNode->visible=false;
  std::get<sg::ExternalImageVisual>(data.sliderImageNode->visual).image=0;
 }
 if(data.alphaImageNode){
  data.alphaImageNode->visible=false;
  std::get<sg::ExternalImageVisual>(data.alphaImageNode->visual).image=0;
 }
 if(data.previewImageNode){
  data.previewImageNode->visible=false;
  std::get<sg::ExternalImageVisual>(data.previewImageNode->visual).image=0;
 }
 if(auto* overlay=findNode(data.generated.component.root(),"Gradient_Overlay_1_64"))
  overlay->visible=true;
}

void FigmaScene::render(const su::InputState& input,
                       su::DrawList& draw,su::Rect viewport,float scale){
 impl_->runtime.render(impl_->generated.component,input,draw,viewport,scale);
}
} // namespace gradation::picker
