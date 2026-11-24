// Pixel-locked, invisible interaction bindings for ColorPicker.slugui.
// NO UI graphics, rectangles, text, fonts, highlights or markers are
// authored here. The exact Figma AOT tree is the sole UI visual source.
// UiContext draws into a disposable scratch DrawList; nothing from this
// function is displayed. All visible values are bound to original Figma
// nodes in figma_scene.cpp (same coordinates / styles / shapes).
#include "picker_ui.hpp"
#include "picker_text_metrics.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>

namespace gradation::picker {
namespace su=slugia;
namespace {
constexpr std::array<float,7> rowY{145.f,177.f,209.f,249.f,281.f,313.f,345.f};
constexpr std::array<float,7> rowW{54.f,54.f,54.f,54.f,54.f,54.f,54.f};
constexpr std::array<const char*,7> fieldIds{"picker.H","picker.S","picker.V",
 "picker.R","picker.G","picker.B","picker.A"};
constexpr std::array<const char*,7> radioIds{
 "picker.radio.hue","picker.radio.saturation","picker.radio.brightness",
 "picker.radio.red","picker.radio.green","picker.radio.blue","picker.radio.alpha"};
inline su::Rect exact(float x,float y,float w,float h,float scale)noexcept{
 return {x*scale,y*scale,w*scale,h*scale};
}
inline bool activated(const su::Interaction& i,const su::InputState& input,
                      bool focused)noexcept{
 return i.clicked||(focused&&(
  input.key(su::Key::Enter).pressed||input.key(su::Key::Space).pressed));
}
inline WidgetState visualState(const su::Interaction& i,bool focused,bool enabled=true)noexcept{
 if(!enabled)return WidgetState::Disabled;
 if(i.pressed||i.held)return WidgetState::Pressed; // Figma "Touch"
 if(focused)return WidgetState::Focus;
 if(i.hovered)return WidgetState::Hover;
 return WidgetState::Normal;
}
inline std::string narrow(const std::wstring& s){
 // Numeric/HEX stop fields are ASCII by contract. Reject unexpected
 // Unicode instead of silently narrowing wchar_t and losing data.
 std::string out;
 out.reserve(s.size());
 for(const wchar_t ch:s){
  if(ch<0||ch>0x7f)return {};
  out.push_back(static_cast<char>(ch));
 }
 return out;
}
inline std::string number(float x,int n){
 char buffer[64]{};
 std::snprintf(buffer,sizeof(buffer),"%.*f",n,(double)x);
 return buffer;
}
} // namespace

void PickerState::sync(slugia::WidgetId focused){
 auto channel=[&](float x)->std::string{
  auto out=narrow(stopui::formatChannel(x,depth));
  // Match AE's grouped 16bpc integer labels, but keep editing lossless.
  if(depth==stopui::Depth::Bpc16&&out.size()>3)
   out.insert(out.size()-3,",");
  return out;
 };
 // The user's exact Figma labels are INTEGER H / S% / B% /
 // 0..255 RGB+A. Do not force 32bpc float strings into 19-29px
 // Source Sans Pro text boxes; the authoritative float RGBA is kept.
 const std::array<std::string,8> next{
   number(std::round(hsv.h),0),
   number(std::round(hsv.s*100.f),0),
   number(std::round(hsv.v*100.f),0),
   channel(current.r),
   channel(current.g),
   channel(current.b),
   channel(current.a),
   narrow(stopui::hexRGB(current))
 };
 for(size_t i=0;i<next.size();++i){
  const auto id=su::hashId(i==7?"picker.HEX":fieldIds[i]);
  if(id!=focused)fields[i]=next[i];
 }
}
bool PickerState::updateField(int i){
 if(i<0||i>=8)return false;
 if(i==7){
  stopui::RGBA next=current;
  const std::wstring text(fields[7].begin(),fields[7].end());
  if(!stopui::parseHexRGB(text.c_str(),next))return false;
  current=next;hsv=deriveHSV(current,hsv);
  return true;
 }
 if(i<3){
  const std::string& s=fields[(size_t)i];
  char* end=nullptr;
  const double x=std::strtod(s.c_str(),&end);
  if(s.empty()||end!=s.c_str()+s.size()||
     !std::isfinite(x)||x<0.0||(i==0?x>360.0:x>100.0))
   return false;
  if(i==0)hsv.h=(float)x;
  else if(i==1)hsv.s=(float)x/100.f;
  else hsv.v=(float)x/100.f;
  current=stopui::fromHSV(hsv,current.a);
  return true;
 }
 // AE channel semantics: integer 255 / 32768, or normalized float32.
 // Thousands separators are display-only, never interpreted as fractions.
 std::string numeric=fields[(size_t)i];
 numeric.erase(std::remove(numeric.begin(),numeric.end(),','),numeric.end());
 const std::wstring wide(numeric.begin(),numeric.end());
 float channel=0.f;
 if(!stopui::parseChannel(wide.c_str(),depth,channel))return false;
 switch(i){
 case 3:current.r=channel;break;
 case 4:current.g=channel;break;
 case 5:current.b=channel;break;
 case 6:current.a=channel;break;
 }
 if(i!=6)hsv=deriveHSV(current,hsv);
 return true;
}

void drawPicker(su::DrawList& /*discarded*/,su::UiContext& ui,
                const su::InputState& input,PickerState& state,float k){
 // Resolve the previous edit BEFORE another interaction takes focus.
 // A click elsewhere commits a valid draft; an invalid partial number is
 // reverted to the last legal current channel instead of blocking OK.
 const int previousNumeric=state.numericFocus;
 const bool previousHex=state.hexFocused;
 if(input.mouse(su::MouseButton::Left).pressed){
  bool keepEdit=false;
  if(previousNumeric>=0){
   const int i=previousNumeric;
   const float w=std::max(63.f,pickerNumericWidth(state.metricsAtlas,state.fields[(size_t)i])+8.f);
   keepEdit=exact(592,rowY[(size_t)i]-4,w+9.f,32,k)
     .contains(input.cursorPosition());
  }else if(previousHex)
   keepEdit=exact(571,384,82,32,k).contains(input.cursorPosition());
  if(!keepEdit&&(previousNumeric>=0||previousHex))
   ui.focus(0);
 }
 // Exact original hit rectangles. Nothing in the visible Figma scene
 // is covered, shifted, resized, recolored or re-created by this code.
 for(int i=0;i<7;++i){
  const auto id=su::hashId(radioIds[(size_t)i]);
  const auto click=ui.interaction(id,exact(544,rowY[(size_t)i],rowW[(size_t)i],24,k));
  const bool focused=ui.focused()==id;
  state.radioStates[(size_t)i]=visualState(click,focused);
  if(activated(click,input,focused))
   state.axis=static_cast<AxisMode>(i);
 }
 const auto plane=ui.interaction(su::hashId("picker.plane"),
                                 exact(23,28,388,388,k));
 const auto channel=ui.interaction(su::hashId("picker.channel"),
                                   exact(436,28,29,388,k));
 const auto alpha=ui.interaction(su::hashId("picker.alpha"),
                                 exact(490,28,29,388,k));
 if(plane.held||plane.pressed){
  applyPlane(state.axis,state.current,state.hsv,
    (plane.cursor.x-23*k)/(388*k),
    (plane.cursor.y-28*k)/(388*k));
  state.sync(ui.focused());
 }else if(channel.held||channel.pressed){
  applyBar(state.axis,state.current,state.hsv,
      (channel.cursor.y-28*k)/(388*k));
  state.sync(ui.focused());
 }else if(alpha.held||alpha.pressed){
  state.current.a=1.f-unit((alpha.cursor.y-28*k)/(388*k));
  state.sync(ui.focused());
 }
 // Invisible Slugia text fields consume the full keyboard/clipboard/IME
 // input while the exported Figma Text nodes remain the only painted text.
 // Do NOT display UiContext's default field graphics.
 for(int i=0;i<7;++i){
  const auto id=su::hashId(fieldIds[(size_t)i]);
  // Restrict interactions to the digits only. The Figma ° and % nodes
  // are independent text siblings, not part of the editable value.
  const bool editing=ui.focused()==id;
  const float valueWidth=pickerNumericWidth(state.metricsAtlas,state.fields[(size_t)i]);
  const auto rect=editing?exact(592,rowY[(size_t)i]-4,
       std::max(72.f,valueWidth+17.f),32,k):
       exact(601,rowY[(size_t)i],valueWidth,24,k);
  const bool clickEdit=input.mouse(su::MouseButton::Left).pressed&&
                       rect.contains(input.cursorPosition());
  const bool wasFocused=ui.focused()==id;
  if(ui.textField(id,rect,state.fields[(size_t)i])){
   state.invalidDraft=!state.updateField(i);
   if(!state.invalidDraft)state.sync(ui.focused());
  }
  // Native AE behavior: first click selects the whole numerical value.
  // Subsequent typing replaces it; a real horizontal drag cancels edit.
  if(clickEdit&&!wasFocused)
   ui.selectAllText(id,state.fields[(size_t)i]);
  // Tiny pointer jitter before mouse-up must not silently cancel the
  // select-all. Scrubbing is a separate >3px gesture transition.
  else if(state.scrubPending==i&&
     input.mouse(su::MouseButton::Left).down&&
     !state.scrubActive&&
     std::abs(input.cursorPosition().x-state.scrubStartX)<=3.f*k)
   ui.selectAllText(id,state.fields[(size_t)i]);
  else if(ui.focused()==id&&input.mouse(su::MouseButton::Left).down&&
          state.scrubPending!=i&&
          (clickEdit||std::abs(input.cursorDelta().x)>.001f)){
   // Editing an already focused field selects arbitrary characters instead
   // of starting another scrub. Values and suffixes share font metrics.
   const auto hit=pickerTextHit(state.metricsAtlas,state.fields[(size_t)i],
                  input.cursorPosition().x/k-601.f);
   const bool extend=!clickEdit||
      input.key(su::Key::LeftShift).down||
      input.key(su::Key::RightShift).down;
   ui.setTextCaret(id,hit,extend);
  }
  if(clickEdit&&!wasFocused){
   state.scrubPending=i;
   state.scrubStartX=input.cursorPosition().x;
   std::string digits=state.fields[(size_t)i];
   digits.erase(std::remove(digits.begin(),digits.end(),','),digits.end());
   state.scrubInitialValue=std::strtof(digits.c_str(),nullptr);
   state.scrubActive=false;
  }
  if(input.mouse(su::MouseButton::Left).down&&state.scrubPending==i){
   const float delta=(input.cursorPosition().x-state.scrubStartX)/k;
   if(std::abs(delta)>3.f)state.scrubActive=true;
   if(state.scrubActive){
    state.scrubIndex=i;
    ui.focus(0); // Scrub and keyboard edit are mutually exclusive.
    const bool fine=input.key(su::Key::LeftShift).down||
                    input.key(su::Key::RightShift).down;
    const float maximum=i==0?360.f:(i<3?100.f:
      (state.depth==stopui::Depth::Bpc32?1.f:
       float(stopui::channelMax(state.depth))));
    const float step=i>=3&&state.depth==stopui::Depth::Bpc32?
      (fine?.001f:.01f):(fine?.1f:1.f);
    const float value=std::clamp(state.scrubInitialValue+
                                delta*(step/2.f),0.f,maximum);
    state.fields[(size_t)i]=number(value,i>=3&&state.depth==stopui::Depth::Bpc32?(fine?3:2):(fine?1:0));
    state.invalidDraft=!state.updateField(i);
    if(!state.invalidDraft)state.sync(0);
   }
  }
 }
 if(input.mouse(su::MouseButton::Left).released){
  state.scrubPending=-1;
  state.scrubIndex=-1;
  state.scrubActive=false;
  state.sync(ui.focused());
 }
 state.numericFocus=-1;
 for(int i=0;i<7;++i){
  const auto id=su::hashId(fieldIds[(size_t)i]);
  if(ui.focused()!=id)continue;
  state.numericFocus=i;
  if(const su::TextEditState* edit=ui.textEditState(id)){
   state.numericCaret[(size_t)i]=edit->caret;
   state.numericSelectionBegin[(size_t)i]=edit->selectionBegin();
   state.numericSelectionEnd[(size_t)i]=edit->selectionEnd();
  }
 }
 const auto hexId=su::hashId("picker.HEX");
 const auto hexRect=exact(571,384,82,32,k);
 const bool hexWasFocused=ui.focused()==hexId;
 const bool hexClick=input.mouse(su::MouseButton::Left).pressed&&
                     hexRect.contains(input.cursorPosition());
 if(ui.textField(hexId,hexRect,state.fields[7])){
  state.invalidDraft=!state.updateField(7);
  if(!state.invalidDraft)state.sync(ui.focused());
 }
 // The HEX sample's six original glyphs occupy exactly 49 authored px.
 // Avoid distributing characters across the current length after deletion.
 if(hexClick&&!hexWasFocused)
  ui.selectAllText(hexId,state.fields[7]);
 else if(ui.focused()==hexId&&input.mouse(su::MouseButton::Left).down&&
   (hexClick||std::abs(input.cursorDelta().x)>.001f)){
  const auto hit=pickerTextHit(state.metricsAtlas,state.fields[7],
                              input.cursorPosition().x/k-584.f,true);
  const bool extend=!input.mouse(su::MouseButton::Left).pressed||
     input.key(su::Key::LeftShift).down||input.key(su::Key::RightShift).down;
  ui.setTextCaret(hexId,hit,extend);
 }
 state.hexFocused=ui.focused()==hexId;
 state.hexCaret=0;
 state.hexSelectionBegin=0;
 state.hexSelectionEnd=0;
 if(state.hexFocused)
  if(const su::TextEditState* edit=ui.textEditState(hexId)){
   state.hexCaret=edit->caret;
   state.hexSelectionBegin=edit->selectionBegin();
   state.hexSelectionEnd=edit->selectionEnd();
  }
 const auto previewId=su::hashId("picker.preview");
 const auto previewInput=ui.interaction(previewId,exact(677,224,90,18,k));
 state.previewState=visualState(previewInput,ui.focused()==previewId);
 if(activated(previewInput,input,ui.focused()==previewId))
  state.preview=!state.preview;
 const auto pipetteId=su::hashId("picker.eyedropper");
 // State screenshots: the Figma eyedropper SVG is centered in a 32px
 // button. It keeps distinct hover/focus/touch states and toggles picking.
 const auto pipetteInput=ui.interaction(pipetteId,exact(687,165,32,32,k));
 state.pipetteState=visualState(pipetteInput,ui.focused()==pipetteId);
 if(activated(pipetteInput,input,ui.focused()==pipetteId))
  state.eyedropper=!state.eyedropper;
 const auto okId=su::hashId("picker.ok");
 const auto okInput=ui.interaction(okId,exact(673,28,101,36,k));
 state.okState=visualState(okInput,ui.focused()==okId,!state.invalidDraft);
 if(!state.invalidDraft&&activated(okInput,input,ui.focused()==okId)){
  state.finished=true;state.accepted=true;
 }
 const auto cancelId=su::hashId("picker.cancel");
 const auto cancelInput=ui.interaction(cancelId,exact(673,81,101,36,k));
 state.cancelState=visualState(cancelInput,ui.focused()==cancelId);
 if(activated(cancelInput,input,ui.focused()==cancelId)){
  state.finished=true;state.accepted=false;
 }
 const bool numericBlurred=previousNumeric>=0&&
      ui.focused()!=su::hashId(fieldIds[(size_t)previousNumeric]);
 const bool hexBlurred=previousHex&&ui.focused()!=hexId;
 if(numericBlurred||hexBlurred){
  // updateField() has already committed valid edit events; synchronize
  // the visible formatted value and drop any incomplete draft on blur.
  state.invalidDraft=false;
  state.sync(ui.focused());
  if(numericBlurred){
   const auto id=su::hashId(fieldIds[(size_t)previousNumeric]);
   ui.resetTextEdit(id,state.fields[(size_t)previousNumeric]);
  }
  if(hexBlurred)ui.resetTextEdit(hexId,state.fields[7]);
 }
}
} // namespace gradation::picker
