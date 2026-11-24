#include <cstring>
// Headless build-time regression: source-authored SlugUI geometry, no HWND,
// no Vulkan renderer, no AE launch, no mouse/keyboard automation.
#include <chrono>
#include <slugia/slugia.hpp>
#include "color_picker.generated.hpp"
#include "button_state.generated.hpp"
#include "figma_scene.hpp"
#include "picker_ui.hpp"
#include "picker_raster.hpp"
#include "picker_renderer_config.hpp"
#include "picker_screen_scale.hpp"
#include "picker_text_metrics.hpp"
#include "picker_theme.hpp"
#include "picker_draw_submission.hpp"
#include "picker_cpu_coverage_experiment.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace sg=slugia::slugui;
namespace su=slugia;
namespace {
const sg::Element* find(const sg::Element& node,std::string_view name){
 if(node.name==name)return &node;
 for(const auto& child:node.children)
  if(auto* result=find(child,name))return result;
 return nullptr;
}
float px(const sg::ValueSource<sg::Length>& value,
         const sg::PropertyStore& source){
 const auto& length=value.resolve(source);
 if(length.unit!=sg::LengthUnit::LogicalPixels)
  throw std::runtime_error("Source Figma dimension is not logical px");
 return length.value;
}
void require(bool value,const char* message){
 if(!value)throw std::runtime_error(message);
}
void dimensions(const sg::Element& root,const sg::PropertyStore& source,
                std::string_view name,float x,float y,float w,float h){
 const auto* element=find(root,name);
 require(element!=nullptr,"Figma source node missing");
 const auto& b=element->layout;
 auto near=[](float a,float b){return std::fabs(a-b)<0.001f;};
 if(!(near(px(b.x,source),x)&&near(px(b.y,source),y)&&
      near(px(b.width,source),w)&&near(px(b.height,source),h)))
   throw std::runtime_error("Figma mismatch "+std::string(name)+
     " expected("+std::to_string(x)+","+std::to_string(y)+","+
     std::to_string(w)+","+std::to_string(h)+") actual("+
     std::to_string(px(b.x,source))+","+
     std::to_string(px(b.y,source))+","+
     std::to_string(px(b.width,source))+","+
     std::to_string(px(b.height,source))+")");
}
// A headless renderer stub exercises the SAME submission API used by the AEX.
// It offers only draw(); any future retained-list regression fails to compile.
struct DirectFrameProbe {
 std::size_t frames=0;
 std::size_t projected=0;
 void draw(const su::DrawList& list) {
  ++frames;
  const auto inspect=[&](const auto& commands) {
   for(const auto& command:commands) {
    const auto* p=std::get_if<su::ProjectedDrawListCommand>(&command);
    if(!p)continue;
    ++projected;
    require(p->content!=nullptr,"Projected Figma command has no content");
    const auto rejectNested=[](const auto& children) {
     for(const auto& child:children) {
      require(!std::holds_alternative<su::ProjectedDrawListCommand>(child),
              "Figma projected DrawList is nested");
      require(!std::holds_alternative<su::RetainedDrawListCommand>(child),
              "Figma projected DrawList nests retained resources");
     }
    };
    rejectNested(p->content->commands());
    rejectNested(p->content->overlayCommands());
   }
  };
  inspect(list.commands());
  inspect(list.overlayCommands());
 }
};
}
int main(){
 try{
  // Keep source-authored sRGB colors in encoded sRGB throughout the modal.
  // This must be explicit: Slugia's default SRGB attachment otherwise
  // applies a second transfer function and brightens the HSV plane/bar.
  const auto baseColorConfig=su::RendererConfig::lowSpec();
  const auto pickerColorConfig=gradation::picker::pickerRendererConfig();
  require(!baseColorConfig.preferSrgbAlphaCompositing&&
           !baseColorConfig.externalImagesAreSrgb&&
           pickerColorConfig.preferSrgbAlphaCompositing&&
           pickerColorConfig.externalImagesAreSrgb,
          "Picker must opt into SRGB_NONLINEAR UNORM presentation without changing Slugia defaults");
  require(pickerColorConfig.enableInProcessPipelineCache&&
          pickerColorConfig.vsync,
          "Preserve lightweight cached and synchronized picker renderer configuration");
  const auto native100=gradation::picker::pickerClientSize(96);
  const auto native150=gradation::picker::pickerClientSize(144);
  require(native100.width==531&&native100.height==296&&
          native150.width==797&&native150.height==444,
          "Authored alpha-inclusive client at standard AE SV plane scale");
  for(unsigned dpi:{96u,120u,144u,192u}){
   const auto size=gradation::picker::pickerClientSize(dpi);
   const float scale=gradation::picker::pickerClientScale(size.width,size.height);
   require(std::abs(size.height-444.f*scale)<=1.f&&
           std::abs(size.width-797.f*scale)<=1.1f,
           "No letterboxed bottom or right gap at native client sizes");
  }
  // Legacy screen fraction helper remains available for other UI layouts.
  require(gradation::picker::pickerScreenScale(3840,2160)==1.f&&
          gradation::picker::pickerScreenScale(1920,1080)==.5f&&
          gradation::picker::pickerScreenScale(2560,1440)>0.66f,
          "Figma viewport must scale with the monitor work area");
  require(gradation::picker::pickerPhysicalWidth(.5f)==399&&
          gradation::picker::pickerPhysicalHeight(.5f)==222,
          "FHD should have proportionate picker dimensions");
  const su::Vec2 originalSize{797.f,444.f};
  const su::Vec2 resized{798.f,444.f};
  require(!gradation::picker::needsPickerFrame(false,false,originalSize,originalSize),
          "Unchanged mouse events must not submit redundant GPU frames");
  require(gradation::picker::needsPickerFrame(true,false,originalSize,originalSize),
          "Changed SlugUI scene must submit a frame");
  require(gradation::picker::needsPickerFrame(false,true,originalSize,originalSize),
          "OS WM_PAINT must redraw even with unchanged source state");
  require(gradation::picker::needsPickerFrame(false,false,resized,originalSize),
          "Resize must force a frame submission");
  su::VectorAtlas atlas;
  gradation::figma::ColorPickerGenerated picker(atlas);
  gradation::figma::ButtonStateGenerated states(atlas);
  const auto& src=picker.component;
  const auto& root=src.root();
  const auto& p=src.properties();
  dimensions(root,p,"Color_Picker_1_42",0.f,0.f,797.f,444.f);
  dimensions(root,p,"Color_Slider_1_55",8.f,5.f,29.f,388.f);
  dimensions(root,p,"Transparency_1_62",8.f,5.f,29.f,388.f);
  dimensions(root,p,"Color_Frame_1_43",0.f,0.f,388.f,388.f);
  dimensions(root,p,"Handle_1_44",400.f,-12.f,25.f,25.f);
  dimensions(root,p,"Frame_6_42_95",544.f,28.f,86.019f,97.f);
  dimensions(root,p,"Group_20_42_82",555.f,384.f,98.f,32.f);
  const auto* previewNode=find(root,"Frame_6_42_95");
  const auto* previewVisual=std::get_if<sg::RoundedRectangleVisual>(&previewNode->visual);
  require(previewVisual&&
          std::abs(previewVisual->stroke.width.resolve(p)-1.f)<.001f&&
          previewVisual->stroke.align==su::StrokeAlign::Inside,
          "Latest full Figma preview uses exact 1px inside stroke");
  const auto border=previewVisual->stroke.paint.normal.resolve(p).start;
  require(std::abs(border.r-75.f/255.f)<1.e-5f&&
          std::abs(border.g-75.f/255.f)<1.e-5f&&
          std::abs(border.b-75.f/255.f)<1.e-5f&&
          std::abs(border.a-1.f)<1.e-5f,
          "Full Figma preview border is opaque #4B4B4B");
  dimensions(root,p,"Rectangle_1_1_169",0.f,48.5f,86.019f,48.5f);
  dimensions(root,p,"Rectangle_3_10_6",0.f,0.f,86.019f,48.5f);
  dimensions(root,p,"Rectangle_2_2_1005",0.f,0.f,82.f,32.f);
  dimensions(root,p,"Group_13_2_990",677.f,224.f,90.f,18.f);
  dimensions(root,p,"Button_Desktop_Darkest_2_814",673.f,28.f,101.f,36.f);
  dimensions(root,p,"Button_Desktop_Darkest_2_816",673.f,81.f,101.f,36.f);
  const auto* frame=find(root,"Color_Frame_1_43");
  auto* frameV=std::get_if<sg::RoundedRectangleVisual>(&frame->visual);
  require(frameV&&frameV->fillLayers.size()==3,
          "Figma 3-layer saturation/brightness gradient not preserved");
  require(frame->clipsChildren,"Figma gradient rounded clip lost");
  const auto* slider=find(root,"Color_Slider_1_55");
  auto* sliderV=std::get_if<sg::RoundedRectangleVisual>(&slider->visual);
  require(sliderV&&
          sliderV->paint.normal.resolve(p).kind==su::GradientKind::LinearStops,
          "Figma 13-stop hue slider replaced or flattened");
  require(sliderV->paint.normal.resolve(p).stops &&
          sliderV->paint.normal.resolve(p).stops->size()==13,
          "Source 13 hue stops not preserved");
  const auto* radio=find(root,"Radio_2_247");
  const auto* unchecked=find(root,"Radio_2_253");
  require(radio&&unchecked&&
          std::holds_alternative<sg::ShapeVisual>(radio->visual)&&
          std::holds_alternative<sg::ShapeVisual>(unchecked->visual),
          "Figma selected/unselected native radio shapes missing");
  const auto& reference=states.component;
  const auto& r=reference.root();
  const auto& rp=reference.properties();
  for(const char* name:{
   "Selection_Not_Selected_Style_None_State_Default_31_1216",
   "Selection_Not_Selected_Style_None_State_Hover_31_1222",
   "Selection_Not_Selected_Style_None_State_Touch_31_1242",
   "Selection_Not_Selected_Style_None_State_Focus_31_1234",
   "Selection_Not_Selected_Style_None_State_Disabled_31_1228",
   "Selection_Selected_Style_Not_Emphasized_State_Default_31_1256",
   "Selection_Selected_Style_Not_Emphasized_State_Hover_31_1280",
   "Selection_Selected_Style_Not_Emphasized_State_Touch_31_1358",
   "Selection_Selected_Style_Not_Emphasized_State_Focus_31_1330",
   "Selection_Selected_Style_Not_Emphasized_State_Disabled_31_1304",
   "Line_1_31_170","Rectangle_5_31_1061"}){
   require(find(r,name)!=nullptr,"Button-State reference variant missing");
  }
  dimensions(r,rp,"Focus_Ring_31_1237",-4.f,5.f,22.f,22.f);
  dimensions(r,rp,"Rectangle_4_31_165",0.f,0.f,82.f,32.f);
  dimensions(r,rp,"Rectangle_5_31_1061",12.f,7.f,49.f,18.f);
  // Exercise the same CPU-only font loading and atlas construction path
  // used by the in-AE picker, without opening any GUI window or GPU.
  // CPU startup profile is headless and never exercises a GUI/Vulkan.
  const auto tBeforeFont=std::chrono::steady_clock::now();
  std::vector<std::uint32_t> glyphs;
  for(std::uint32_t c=32;c<127;++c)glyphs.push_back(c);
  glyphs.push_back(176);
  require(atlas.loadSystemFont("Source Sans Pro",400,false,glyphs,"Regular"),
          "Picker font initialization: Source Sans Pro Regular failed");
  require(atlas.loadSystemFont("Source Sans Pro",600,false,glyphs,"SemiBold"),
          "Picker font initialization: Source Sans Pro SemiBold failed");
  require(atlas.loadSystemFont("Source Sans Pro",700,false,glyphs,"Bold"),
          "Picker font initialization: Source Sans Pro Bold failed");
  const auto tFontsReady=std::chrono::steady_clock::now();
  gradation::picker::FigmaScene dialogScene(atlas);
  const auto tSceneReady=std::chrono::steady_clock::now();
  gradation::picker::PickerState model;
  model.sync();
  atlas.build();
  const auto tAtlasReady=std::chrono::steady_clock::now();
  require(dialogScene.update(model),"Picker initial state update must generate visuals");
  su::InputState input;
  su::DrawList draw;
  dialogScene.render(input,draw,{0.f,0.f,797.f,444.f},1.f);
  const auto tFirstFrame=std::chrono::steady_clock::now();
  auto milliseconds=[](auto a,auto b){
   return std::chrono::duration<double,std::milli>(b-a).count();
  };
  std::cout<<"STARTUP_CPU_MS fonts="<<milliseconds(tBeforeFont,tFontsReady)
           <<" figma_scene="<<milliseconds(tFontsReady,tSceneReady)
           <<" atlas="<<milliseconds(tSceneReady,tAtlasReady)
           <<" first_projection="<<milliseconds(tAtlasReady,tFirstFrame)
           <<" (headless; excludes Vulkan)\n";
  // First GPU-free backend component: the actual authored Figma atlas
  // shape masks, including fractional placement and both reference DPIs.
  {
   using namespace gradation::picker::cpu_coverage;
   std::vector<su::DrawCommand> shapes;
   auto collect=[&](auto&& self,const su::DrawList& list)->void{
    auto visit=[&](const auto& cmds){
     for(const auto& cmd:cmds){
      if(const auto* shape=std::get_if<su::DrawCommand>(&cmd)){
       if(shape->destination.width>0&&shape->destination.height>0&&
          shape->destination.width<=256&&shape->destination.height<=256)
        shapes.push_back(*shape);
      }else if(const auto* p=std::get_if<su::ProjectedDrawListCommand>(&cmd)){
       if(p->content)self(self,*p->content);
      }
     }
    };
    visit(list.commands());visit(list.overlayCommands());
   };
   collect(collect,draw);
   require(!shapes.empty(),"Expected authored atlas shapes for CPU backend");
   size_t pixels=0;float maxError=0.f;double totalMs=0.;
   for(const auto& command:shapes){
    const auto sampler=slughorn::render::decode(atlas.native(),slughorn::Key(command.shape));
    for(float scale:{.6f,.9f}){
     const su::Rect dst{.25f,.375f,command.destination.width*scale,command.destination.height*scale};
     const auto t0=std::chrono::steady_clock::now();
     auto actual=rasterize(sampler,dst,true);
     totalMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
     auto reference=rasterize(sampler,dst,false);
     require(actual.values.size()==reference.values.size(),"CPU coverage dimensions");
     for(size_t i=0;i<actual.values.size();++i){
      require(std::isfinite(actual.values[i]),"Finite CPU coverage");
      maxError=std::max(maxError,std::abs(actual.values[i]-reference.values[i]));
     }
     pixels+=actual.values.size();
    }
   }
   std::cout<<"CPU_COVERAGE_EXPERIMENT shapes="<<shapes.size()
      <<" samples="<<pixels<<" banded_ms="<<totalMs<<" full_curve_max_abs="<<maxError
      <<" (not a full UI renderer or GPU parity claim)\n";
  }
  DirectFrameProbe probe;
  gradation::picker::submitPickerDrawList(probe,draw);
  require(probe.projected>0,
          "ColorPicker must exercise projected SlugUI frame commands");
  // The runtime border, not the immutable Figma source stroke, is the
  // requested 1px INSIDE, 100% opaque #4B4B4B front-most contour.
  int correctBorders=0;
  const auto checkBorders=[&](const auto& commands){
   for(const auto& command:commands){
    if(const auto* rect=std::get_if<su::RoundedRectCommand>(&command)){
     const auto& b=rect->border;
     const auto& c=b.paint.start;
     const float target=75.f/255.f;
     if(std::fabs(b.width-1.f)<1e-5f&&
        b.align==su::StrokeAlign::Inside&&
        std::fabs(c.r-target)<1e-5f&&
        std::fabs(c.g-target)<1e-5f&&
        std::fabs(c.b-target)<1e-5f&&
        std::fabs(c.a-1.f)<1e-5f&&
        std::fabs(b.paint.opacity-1.f)<1e-5f)
      ++correctBorders;
    }
   }
  };
  checkBorders(draw.commands());
  for(const auto& command:draw.commands())
   if(const auto* projected=std::get_if<su::ProjectedDrawListCommand>(&command))
    if(projected->content){
     checkBorders(projected->content->commands());
     checkBorders(projected->content->overlayCommands());
    }
  require(correctBorders>=3,
          "Expected three 1px inside #4B4B4B 100%-opaque ColorPicker borders");
  // Re-run the actual scene generation path for another color-axis mode.
  // The dynamic 96x6 plane must not change to an illegal retained route.
  model.axis=gradation::picker::AxisMode::Saturation;
  model.sync();
  require(dialogScene.update(model),"Saturation mode must update native scene");
  draw.clear();
  dialogScene.render(input,draw,{0.f,0.f,797.f,444.f},1.f);
  gradation::picker::submitPickerDrawList(probe,draw);
  require(probe.frames==2&&probe.projected>0,
          "Hue and Saturation frame submissions must use direct projection");
  // The latest full source specifies opaque #4B4B4B 1px INSIDE borders.
  for(const char* name:{"Color_Frame_1_43","Color_Slider_1_55",
                        "Transparency_1_62","Frame_6_42_95"}){
   const auto* e=find(root,name);
   require(e!=nullptr,"Missing latest source frame/bar for border audit");
   const auto* v=std::get_if<sg::RoundedRectangleVisual>(&e->visual);
   require(v!=nullptr,"Source border must remain an authored shape");
   const auto stroke=v->stroke.paint.normal.resolve(p);
   const auto& c=stroke.start;
   require(std::fabs(c.r-75.f/255.f)<1.e-5f&&
           std::fabs(c.g-75.f/255.f)<1.e-5f&&
           std::fabs(c.b-75.f/255.f)<1.e-5f&&
           std::fabs(c.a*stroke.opacity-1.f)<1.e-5f&&
           std::fabs(v->stroke.width.resolve(p)-1.f)<1.e-5f&&
           v->stroke.align==su::StrokeAlign::Inside,
           "Latest full-source border must preserve 1px #4B4B4B");
  }
  // Clicking a numeric value selects all immediately; new typed codepoint
  // replaces selection with a valid editable value (not a drag/scrub).
  {
   su::UiSkin skin{};skin.text.size=12.f;
   su::UiContext editor(skin);
   su::InputState st;
   su::InputWriter writer(st);
   su::DrawList sink;
   const auto id=su::hashId("picker.H");
   std::string value="178";
   writer.beginFrame();
   writer.cursor({605.f,155.f});
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Press);
   writer.finishFrame(0.);
   editor.beginFrame(st,sink);
   editor.textField(id,{600,143,79,24},value);
   editor.selectAllText(id,value);
   const auto* edit=editor.textEditState(id);
   require(edit&&edit->selectionBegin()==0&&edit->selectionEnd()==3,
           "Numeric click must begin with full selection");
   editor.endFrame();
   writer.beginFrame();
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Release);
   writer.finishFrame(.016);
   sink.clear();editor.beginFrame(st,sink);
   editor.textField(id,{600,143,79,24},value);
   editor.endFrame();
   writer.beginFrame();
   writer.codepoint(U'5');
   writer.finishFrame(.032);
   sink.clear();editor.beginFrame(st,sink);
   require(editor.textField(id,{600,143,79,24},value),
           "Typed numeric character must replace full selection");
   editor.endFrame();
   require(value=="5","Numeric input must be editable after click-select-all");
  }
  require(gradation::picker::numericTextHit("29,341",0.f)==0&&
          gradation::picker::numericTextHit("29,341",25.f)==3&&
          std::fabs(gradation::picker::numericTextWidth("178")-28.65f)<.01f,
          "Native numeric hit test and suffix advances must be consistent");
  require(gradation::picker::numericValueContains("178",607.f,155.f,143.f)&&
          !gradation::picker::numericValueContains("178",635.f,155.f,143.f),
          "Digit edit hit test must exclude the independently authored degree sign");
  require(gradation::picker::numericValueContains("90",607.f,187.f,175.f)&&
          !gradation::picker::numericValueContains("90",628.f,187.f,175.f),
          "Digit edit hit test must exclude the percent sign");
  {
   // Full picker input routing, not merely the text field's isolated API.
   const auto field=su::hashId("picker.H");
   su::InputState pointer;
   su::InputWriter writer(pointer);
   su::UiContext pickerUi(su::UiSkin{});
   pickerUi.setInputOnly(true);
   su::DrawList scratch;
   gradation::picker::PickerState state;
   state.hsv.h=178.f;
   state.sync();
   writer.beginFrame();
   writer.cursor({635.f,155.f}); // degree symbol, not digits
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Press);
   writer.finishFrame(0.);
   pickerUi.beginFrame(pointer,scratch);
   gradation::picker::drawPicker(scratch,pickerUi,pointer,state,1.f);
   pickerUi.endFrame();
   require(pickerUi.focused()!=field,
           "Clicking degree suffix may not focus, select, or edit numeric text");
   writer.beginFrame();
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Release);
   writer.finishFrame(.01);
   scratch.clear();pickerUi.beginFrame(pointer,scratch);
   gradation::picker::drawPicker(scratch,pickerUi,pointer,state,1.f);
   pickerUi.endFrame();
   writer.beginFrame();
   writer.cursor({607.f,155.f}); // only the H digits
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Press);
   writer.finishFrame(.02);
   scratch.clear();pickerUi.beginFrame(pointer,scratch);
   gradation::picker::drawPicker(scratch,pickerUi,pointer,state,1.f);
   pickerUi.endFrame();
   const auto* edit=pickerUi.textEditState(field);
   require(scratch.commands().empty()&&scratch.overlayCommands().empty(),
           "Interaction-only picker must issue zero Slugia UI draw commands");
   require(pickerUi.focused()==field&&edit&&
           edit->selectionBegin()==0&&
           edit->selectionEnd()==state.fields[0].size(),
           "Clicking H numeric text must select its digits only");
  }
  // UI fields must use AE's real per-bit-depth channel ranges.
  {
   gradation::picker::PickerState numeric;
   numeric.depth=gradation::stopui::Depth::Bpc16;
   numeric.current.g=29341.f/32768.f;
   numeric.sync();
   require(numeric.fields[4]=="29,341",
           "16bpc channel label must use AE 32768 with grouping");
   numeric.fields[4]="30,000";
   require(numeric.updateField(4)&&
      std::fabs(numeric.current.g-30000.f/32768.f)<1.e-5f,
      "16bpc value must accept thousands separator and full channel precision");
   numeric.depth=gradation::stopui::Depth::Bpc32;
   numeric.fields[6]="0.125";
   require(numeric.updateField(6)&&
       std::fabs(numeric.current.a-.125f)<1.e-6f,
       "32bpc alpha must accept normalized fractional input");
  }
  // Full native input routing: clicking selects only digits; WM_CHAR-style
  // codepoint input replaces the value, and clicking elsewhere commits.
  {
   su::UiSkin skin{};
   su::UiContext editor(skin);
   editor.setInputOnly(true);
   su::InputState input;
   su::InputWriter writer(input);
   su::DrawList scratch;
   gradation::picker::PickerState model;
   model.hsv.h=178.f;model.sync();
   auto tick=[&](double t){
    writer.finishFrame(t);
    scratch.clear();
    editor.beginFrame(input,scratch);
    gradation::picker::drawPicker(scratch,editor,input,model,1.f);
    editor.endFrame();
    require(scratch.commands().empty(),
            "External editor cannot allocate hidden vector UI");
   };
   writer.beginFrame();
   writer.cursor({606.f,155.f});
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Press);
   tick(0.);
   require(model.numericFocus==0,"Numeric click did not obtain text focus");
   writer.beginFrame();
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Release);
   tick(.016);
   writer.beginFrame();
   writer.codepoint(U'5'); // exactly the Win32 WM_CHAR mapping
   tick(.032);
   require(model.fields[0]=="5"&&std::abs(model.hsv.h-5.f)<.001f,
           "Win32-style codepoint input failed to replace selected digits");
   writer.beginFrame();
   writer.cursor({300.f,420.f});
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Press);
   tick(.048);
   require(model.numericFocus<0&&!model.invalidDraft&&model.fields[0]=="5",
           "Click outside text editor must commit numeric value");
  }
  {
   gradation::picker::PickerState model;
   model.depth=gradation::stopui::Depth::Bpc32;
   model.current.g=.80654321f;
   model.sync();
   require(model.fields[4]=="0.8065",
           "32bpc displayed precision must be capped at four decimals");
  }
  // Cache reuse resets renderer-only image IDs but keeps CPU geometry intact.
  dialogScene.resetRendererResources();
  require(dialogScene.update(model),"CPU-only Figma scene reset must repaint");
  // ExternalImageVisual must reach the ordinary Slugia DrawList without
  // rebuilding a grid of rounded rectangles or opening a GPU window.
  {
   auto root=sg::absolute(su::hashId("external-test-root"),"ExternalRoot");
   root.layout.width=sg::Length::logical(100.f);
   root.layout.height=sg::Length::logical(80.f);
   auto child=sg::absolute(su::hashId("external-test-image"),"ExternalImage");
   child.layout.x=sg::Length::logical(11.f);
   child.layout.y=sg::Length::logical(12.f);
   child.layout.width=sg::Length::logical(20.f);
   child.layout.height=sg::Length::logical(16.f);
   child.visual=sg::ExternalImageVisual{7,20,16};
   root.children.push_back(std::move(child));
   sg::Component comp(std::move(root));
   sg::Runtime runtime;
   su::DrawList list;
   runtime.render(comp,input,list,{0,0,100,80},1.f);
   bool emitted=false;
   for(const auto& c:list.commands())if(const auto* im=std::get_if<su::ExternalImageCommand>(&c)){
    emitted|=im->image==7&&im->width==20&&im->height==16&&
             std::fabs(im->destination.x-11.f)<.01f;
   }
   require(emitted,"Single-image SlugUI gradient path must emit an external image");
  }
  // Drag selection and native editing state are verified without a GUI.
  {
   su::UiSkin skin;skin.text.size=16.f;
   su::UiContext editor(skin);
   su::InputState state;
   su::InputWriter writer(state);
   su::DrawList sink;
   std::string value="FF00AA";
   writer.beginFrame();
   writer.cursor({9.f,10.f});
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Press);
   writer.finishFrame(0.);
   editor.beginFrame(state,sink);
   editor.textField(su::hashId("edit-test"),{0,0,100,24},value);
   editor.endFrame();
   writer.beginFrame();
   writer.cursor({70.f,10.f});
   writer.finishFrame(.016);
   sink.clear();
   editor.beginFrame(state,sink);
   editor.textField(su::hashId("edit-test"),{0,0,100,24},value);
   const auto* e=editor.textEditState(su::hashId("edit-test"));
   require(e&&e->hasSelection()&&e->selectionBegin()==0,
           "Text field horizontal mouse dragging must select a range");
   editor.endFrame();
   writer.beginFrame();
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Release);
   writer.finishFrame(.032);
   sink.clear();
   editor.beginFrame(state,sink);
   editor.textField(su::hashId("edit-test"),{0,0,100,24},value);
   editor.endFrame();
  }
  const auto& sourceImageCommands=draw.commands();
  require(sourceImageCommands.size()<500,
          "Saturation gradient cannot regress to hundreds of AA band cells");
  // General-purpose Slugia widgets must preserve input semantics while an
  // external SlugUI/Figma renderer owns visuals. No hidden DrawList commands,
  // clips, overlays, labels or glyph allocations may be recorded.
  const auto checkInputOnly = [](su::Vec2 pos, const auto& invoke){
   su::UiContext controls(su::UiSkin{});
   controls.setInputOnly(true);
   su::DrawList sink;
   su::InputState input;
   su::InputWriter writer(input);
   writer.beginFrame();
   writer.cursor(pos);
   writer.mouseButton(su::MouseButton::Left,su::InputAction::Press);
   writer.finishFrame(0.);
   controls.beginFrame(input,sink);
   require(invoke(controls),"Input-only widget lost its input semantics");
   controls.endFrame();
   require(sink.commands().empty()&&sink.overlayCommands().empty(),
           "Input-only widget emitted hidden GPU draw commands");
  };
  checkInputOnly({12,12},[](su::UiContext& c){
   return c.button(su::hashId("fast.button"),"Button",{0,0,120,30});
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   int value=7;
   return c.spinButton(su::hashId("fast.spin"),"Value",{0,0,120,30},value,0,100,1)&&value==6;
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   float value=.8f;
   return c.slider(su::hashId("fast.slider"),"",{0,0,120,30},value,0,1)&&value<.8f;
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   bool checked=false;
   return c.checkbox(su::hashId("fast.checkbox"),"Check",{0,0,120,30},checked)&&checked;
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   return c.radio(su::hashId("fast.radio"),"Option",{0,0,120,30},false);
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   bool toggled=false;
   return c.toggle(su::hashId("fast.toggle"),"On",{0,0,120,30},toggled)&&toggled;
  });
  checkInputOnly({6,75},[](su::UiContext& c){
   float pos=0.f;
   return c.scrollBar(su::hashId("fast.scroll"),{0,0,12,100},pos,.2f)&&pos>0.f;
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   su::ListBoxModel model;
   model.items={"first","second"};
   return c.listBox(su::hashId("fast.list"),{0,0,120,80},model)&&model.selected==0;
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   su::ComboBoxModel model;
   model.items={"first","second"};
   c.comboBox(su::hashId("fast.combo"),{0,0,120,30},model);
   return model.open;
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   std::vector<su::TreeNode> roots(1);
   roots[0].label="Root";
   const bool changed=c.treeView(su::hashId("fast.tree"),{0,0,120,80},roots);
   return changed&&roots[0].selected;
  });
  checkInputOnly({12,40},[](su::UiContext& c){
   su::GridModel model;
   model.headers={"Name"};
   model.rows={{"One"}};
   return c.gridView(su::hashId("fast.grid"),{0,0,120,80},model)&&
          model.selectedRow==0&&model.selectedColumn==0;
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   auto id=su::hashId("fast.area");
   std::string text="Ready";
   su::Vec2 scroll{};
   c.textArea(id,{0,0,120,60},text,scroll);
   return c.focused()==id;
  });
  checkInputOnly({12,12},[](su::UiContext& c){
   auto id=su::hashId("fast.tip");
   c.interaction(id,{0,0,120,24});
   c.tooltip(id,"Tip",{0,0,80,20});
   return c.hovered()==id;
  });
  {
   su::UiContext drawn(su::UiSkin{});
   su::DrawList visible;
   su::InputState state;
   drawn.beginFrame(state,visible);
   drawn.button(su::hashId("fast.normal"),"Visible",{0,0,120,30});
   drawn.endFrame();
   require(!visible.commands().empty(),
           "Non-input-only Slugia widgets must still render normally");
  }
  // Deterministic headless CPU microbenchmark (no AE/Vulkan window).
  // This measures only UiContext recording, not GPU/frame startup.
  const auto benchRecording=[](bool inputOnly){
   su::UiSkin skin{};
   su::UiContext controls(skin);
   controls.setInputOnly(inputOnly);
   su::DrawList sink;
   su::InputState state;
   su::InputWriter writer(state);
   bool checked=false;
   float value=.5f;
   std::string edit="123";
   std::size_t recorded=0;
   constexpr int iterations=20000;
   const auto start=std::chrono::steady_clock::now();
   for(int i=0;i<iterations;++i){
    writer.beginFrame();
    writer.cursor({-100,-100}); // unchanged idle cursor
    writer.finishFrame(double(i)*.016);
    sink.clear();
    controls.beginFrame(state,sink);
    controls.button(su::hashId("bench.button"),"Button",{0,0,100,25});
    controls.checkbox(su::hashId("bench.check"),"Check",{0,30,100,25},checked);
    controls.slider(su::hashId("bench.slider"),"Value",{0,60,150,25},value);
    controls.textField(su::hashId("bench.edit"),{0,90,100,25},edit);
    controls.endFrame();
    recorded+=sink.commands().size()+sink.overlayCommands().size();
   }
   const auto elapsed=std::chrono::steady_clock::now()-start;
   return std::pair<double,std::size_t>{
     std::chrono::duration<double,std::micro>(elapsed).count()/iterations,
     recorded};
  };
  // Repeated runs reduce cold-cache bias; comparisons are only exploratory.
  double normalUs=0.,inputUs=0.;
  std::size_t normalCommands=0,inputCommands=0;
  for(int run=0;run<3;++run){
   const auto normal=benchRecording(false);
   const auto inputOnly=benchRecording(true);
   normalUs+=normal.first;
   inputUs+=inputOnly.first;
   normalCommands+=normal.second;
   inputCommands+=inputOnly.second;
  }
  normalUs/=3.;
  inputUs/=3.;
  require(normalCommands>0&&inputCommands==0,
          "Input-only CPU recording must avoid all draw commands");
  std::cout<<"BENCH Slugia UiContext 4 idle widgets: normal="
           <<normalUs<<" us/frame, input-only="<<inputUs
           <<" us/frame, normal-command-count="<<normalCommands
           <<", input-only-command-count="<<inputCommands<<" (3x20000 headless frames)\n";
  // A reused raster key must imply byte-identical RGBA32f output.
  // Exercise independent parameter changes and actual plane/bar gestures.
  {
   using namespace gradation::picker;
   int compared=0,planeReuses=0;
   PickerState initial;
   initial.current={.23f,.71f,.39f,.62f};
   initial.hsv=gradation::stopui::toHSV(initial.current);
   initial.initial={.8f,.1f,.3f,.4f};
   for(int axis=0;axis<7;++axis){
    initial.axis=(AxisMode)axis;
    for(int mutation=0;mutation<12;++mutation){
     auto next=initial;
     switch(mutation){
      case 0:next.current.r=.91f;break;
      case 1:next.current.g=.12f;break;
      case 2:next.current.b=.81f;break;
      case 3:next.current.a=.25f;break;
      case 4:next.hsv.h=45.f;break;
      case 5:next.hsv.s=.83f;break;
      case 6:next.hsv.v=.15f;break;
      case 7:next.initial.r=.11f;break;
      case 8:next.preview=false;break;
      case 9:applyPlane(next.axis,next.current,next.hsv,.43f,.71f);break;
      case 10:applyBar(next.axis,next.current,next.hsv,.21f);break;
      case 11:next.theme=2;break;
     }
     for(int kind=0;kind<4;++kind){
      if(pickerRasterKey(initial,kind)!=pickerRasterKey(next,kind))continue;
      const int w=kind==0?386:kind==3?172:27,h=kind==3?97:386;
      std::vector<float> a(size_t(w)*h*4u),b(a.size());
      rasterizePickerImage(initial,w,h,kind,a);
      rasterizePickerImage(next,w,h,kind,b);
      require(std::memcmp(a.data(),b.data(),a.size()*sizeof(float))==0,
              "Equal picker raster keys must imply byte-identical images");
      ++compared;
      if(kind==0&&mutation==9)++planeReuses;
     }
    }
   }
   require(planeReuses==7,"Dragging within any plane must reuse its raster");
   std::cout<<"PASS "<<compared<<" exact raster dependency comparisons; "
            <<planeReuses<<" plane drag reuse paths\\n";
  }
  // UI-only paint change benchmark: the source tree/geometry remains fixed.
  // This is a CPU benchmark, not a GPU or Adobe host measurement.
  {
   gradation::picker::PickerState hoverModel;
   hoverModel.sync();
   dialogScene.update(hoverModel);
   constexpr int iterations=12000;
   double totalUs=0.;
   for(int repeat=0;repeat<3;++repeat){
    const auto started=std::chrono::steady_clock::now();
    for(int i=0;i<iterations;++i){
     hoverModel.radioStates[0]=(i&1)?gradation::picker::WidgetState::Normal:
                                         gradation::picker::WidgetState::Hover;
     if(!dialogScene.update(hoverModel))
      throw std::runtime_error("UI hover transition was not repainted");
    }
    const auto elapsed=std::chrono::steady_clock::now()-started;
    totalUs+=std::chrono::duration<double,std::micro>(elapsed).count()/iterations;
   }
   std::cout<<"BENCH Figmascene one radio hover: "<<totalUs/3.
            <<" us/update (3 x 12000 CPU-only updates)\n";
  }
  // Production 32f CPU color-raster math must preserve the original
  // per-pixel color results for all seven axes and all four image kinds.
  // The reference below intentionally repeats the prior per-pixel work.
  {
   using namespace gradation::picker;
   PickerState pixels{};
   pixels.current={.15f,.7f,.33f,.6f};
   pixels.initial={.2f,.4f,.9f,.4f};
   pixels.hsv={178.f,.91f,.76f};
   std::vector<float> actual,reference;
   auto oldRaster=[&](int w,int h,int kind){
    reference.resize(size_t(w)*size_t(h)*4u);
    const auto hueBase=gradation::stopui::fromHSV({pixels.hsv.h,1.f,1.f},1.f);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
     const float u=(float(x)+.5f)/float(w);
     const float v=(float(y)+.5f)/float(h);
     RGBA c{};
     if(kind==3){
      const auto c0=pixels.preview?pixels.current:pixels.initial;
      const float a=gradation::stopui::uiClamp(c0.a);
      const float scale=(w==172&&h==97)?2.f:1.f;
      const float authored=31.f/3.f;
      const int xx=int(std::floor((float(x)+.5f)/(authored*scale)));
      const int yy=int(std::floor((float(y)+.5f)/(authored*scale)));
      const float checker=((xx+yy)&1)==0?1.f:213.f/255.f;
      c={checker*(1.f-a)+gradation::stopui::uiClamp(c0.r)*a,
         checker*(1.f-a)+gradation::stopui::uiClamp(c0.g)*a,
         checker*(1.f-a)+gradation::stopui::uiClamp(c0.b)*a,1.f};
     }else if(kind==2){
      const float authored=31.f/3.f;
      const int xx=int(std::floor((float(x)+.5f)/authored));
      const int yy=int(std::floor((float(y)+.5f)/authored));
      const float tile=((xx+yy)&1)==0?1.f:213.f/255.f;
      const float a=1.f-v;
      c={tile*(1.f-a)+gradation::stopui::uiClamp(pixels.current.r)*a,
         tile*(1.f-a)+gradation::stopui::uiClamp(pixels.current.g)*a,
         tile*(1.f-a)+gradation::stopui::uiClamp(pixels.current.b)*a,1.f};
     }else if(pixels.axis==AxisMode::Hue){
      if(kind==1)c=gradation::stopui::fromHSV({360.f*(1.f-v),1.f,1.f},1.f);
      else {
       const float value=1.f-v;
       c={value*((1.f-u)+u*hueBase.r),value*((1.f-u)+u*hueBase.g),
          value*((1.f-u)+u*hueBase.b),1.f};
      }
     }else c=kind==0?samplePlane(pixels.axis,pixels.current,pixels.hsv,u,v):
                             sampleBar(pixels.axis,pixels.current,pixels.hsv,v);
     const size_t j=(size_t(y)*w+x)*4u;
     reference[j]=gradation::stopui::uiClamp(c.a);
     reference[j+1]=gradation::stopui::uiClamp(c.r);
     reference[j+2]=gradation::stopui::uiClamp(c.g);
     reference[j+3]=gradation::stopui::uiClamp(c.b);
    }
   };
   for(int axis=0;axis<7;++axis){
    pixels.axis=static_cast<AxisMode>(axis);
    pixels.preview=(axis&1)==0;
    for(int kind=0;kind<4;++kind){
     const int w=kind==0?386:kind==3?86:27;
     const int h=kind==3?49:386;
     oldRaster(w,h,kind);
     actual.resize(size_t(w)*size_t(h)*4u);
     rasterizePickerImage(pixels,w,h,kind,actual);
     require(actual.size()==reference.size(),
             "Raster buffer size must match the legacy layout");
     for(size_t i=0;i<actual.size();++i)
      if(actual[i]!=reference[i])
       throw std::runtime_error("Raster mismatch axis "+std::to_string(axis)+
                                " kind "+std::to_string(kind)+
                                " component "+std::to_string(i)+
                                " old "+std::to_string(reference[i])+
                                " new "+std::to_string(actual[i]));
    }
   }
   // 48.5px authored preview is now rasterized at 2x (172x97) so the
   // half-pixel split and checker spacing are not rounded to 49px.
   oldRaster(172,97,3);
   actual.resize(172u*97u*4u);
   rasterizePickerImage(pixels,172,97,3,actual);
   for(size_t i=0;i<actual.size();++i)
    require(actual[i]==reference[i],
            "Hi-resolution preview checker must exactly match authored tiles");
   pixels.axis=AxisMode::Hue;
   pixels.preview=true;
   constexpr int w=27,h=386,iterations=240;
   actual.resize(size_t(w)*h*4u);
   // Benchmark both versions with unchanged dimensions and warm instruction
   // caches, alternating runs to reduce frequency/turbo skew.
   double oldTotal=0.,newTotal=0.;
   for(int rep=0;rep<3;++rep){
    auto t0=std::chrono::steady_clock::now();
    for(int i=0;i<iterations;++i)oldRaster(w,h,1);
    auto t1=std::chrono::steady_clock::now();
    for(int i=0;i<iterations;++i)rasterizePickerImage(pixels,w,h,1,actual);
    auto t2=std::chrono::steady_clock::now();
    oldTotal+=std::chrono::duration<double,std::micro>(t1-t0).count()/iterations;
    newTotal+=std::chrono::duration<double,std::micro>(t2-t1).count()/iterations;
   }
   std::cout<<"BENCH Hue bar RGBA32f: old="<<oldTotal/3.
            <<"us new="<<newTotal/3.
            <<"us (3 x 240 CPU runs); exact RGB32f parity: 7 axes x 4 raster kinds\n";
   pixels.axis=AxisMode::Saturation;
   constexpr int planeW=386,planeH=386,planeIterations=40;
   actual.resize(size_t(planeW)*planeH*4u);
   double oldPlaneUs=0.,newPlaneUs=0.;
   for(int rep=0;rep<3;++rep){
    auto t0=std::chrono::steady_clock::now();
    for(int i=0;i<planeIterations;++i)oldRaster(planeW,planeH,0);
    auto t1=std::chrono::steady_clock::now();
    for(int i=0;i<planeIterations;++i)
     rasterizePickerImage(pixels,planeW,planeH,0,actual);
    auto t2=std::chrono::steady_clock::now();
    oldPlaneUs+=std::chrono::duration<double,std::micro>(t1-t0).count()/planeIterations;
    newPlaneUs+=std::chrono::duration<double,std::micro>(t2-t1).count()/planeIterations;
   }
   std::cout<<"BENCH Saturation plane RGBA32f: old="<<oldPlaneUs/3.
            <<"us new="<<newPlaneUs/3.<<"us (3 x 40 CPU runs)\n";
  }
  // AE's UI appearance can change between light, gray and dark without
  // rebuilding the source Figma tree or altering the selected RGB value.
  const auto beforeThemeColor=model.current;
  for(int theme:{1,2,0}){
   model.theme=theme;
   require(dialogScene.update(model),"AE theme transition must repaint color picker");
   draw.clear();
   dialogScene.render(input,draw,{0.f,0.f,797.f,444.f},1.f);
   require(!draw.commands().empty(),"AE theme switch must project a frame");
   const auto palette=gradation::picker::pickerPalette(theme);
   // Exercise actual text and selection projection at native UI scale.
   model.metricsAtlas=&atlas;
   model.numericFocus=4;
   model.hexFocused=false;
   model.fields[4]="0.8065";
   model.numericSelectionBegin[4]=2;
   model.numericSelectionEnd[4]=4;
   require(dialogScene.update(model),"Numeric selection must update the scene");
   draw.clear();
   dialogScene.render(input,draw,{0.f,0.f,797.f,444.f},1.f);
   bool selectedDigits=false,normalDigits=false,selectionRect=false,focusBox=false;
   const auto sameColor=[](const su::Color& a,std::uint32_t rgb){
    const auto b=su::Color::fromRgb8(rgb);
    return std::fabs(a.r-b.r)<1.e-5f&&std::fabs(a.g-b.g)<1.e-5f&&
           std::fabs(a.b-b.b)<1.e-5f&&a.a>.99f;
   };
   const auto expectedWidth=gradation::picker::pickerTextAdvance(
     &atlas,model.fields[4],4)-gradation::picker::pickerTextAdvance(
     &atlas,model.fields[4],2);
   const auto inspect=[&](auto& self,const auto& commands)->void{
    for(const auto& c:commands){
     if(const auto* t=std::get_if<su::TextCommand>(&c)){
      if(t->runs.size()==3&&t->runs[0].text=="0."&&
         t->runs[1].text=="80"&&t->runs[2].text=="65"){
       selectedDigits=sameColor(t->runs[1].style.paint.start,palette.selectedText);
       normalDigits=sameColor(t->runs[0].style.paint.start,palette.fieldText)&&
                    sameColor(t->runs[2].style.paint.start,palette.fieldText);
      }
     }else if(const auto* rect=std::get_if<su::RoundedRectCommand>(&c)){
      if(sameColor(rect->paint.start,palette.selection)&&
         std::fabs(rect->destination.width-expectedWidth)<.05f)
       selectionRect=true;
      if(sameColor(rect->paint.start,palette.field)&&
         sameColor(rect->border.paint.start,palette.focus)&&
         rect->border.width>2.f)focusBox=true;
     }else if(const auto* projection=std::get_if<su::ProjectedDrawListCommand>(&c))
      if(projection->content){
       self(self,projection->content->commands());
       self(self,projection->content->overlayCommands());
      }
    }
   };
   inspect(inspect,draw.commands());inspect(inspect,draw.overlayCommands());
   require(selectedDigits&&normalDigits&&selectionRect&&focusBox,
      "Actual numeric draw commands must color only selected digits and use measured width");
   // Theme transitions never change selected color or mutate its source design.
   model.numericFocus=-1;
   model.hexFocused=false;

   require(model.current.r==beforeThemeColor.r&&
           model.current.g==beforeThemeColor.g&&
           model.current.b==beforeThemeColor.b&&
           model.current.a==beforeThemeColor.a,
           "AE theme transition must never change RGBA values");
  }
  std::cout<<"PASS: authored ColorPicker/State source, font atlas, native AE 3-theme repaint, and direct projection (Hue/Saturation)\\n";
  return EXIT_SUCCESS;
 }catch(const std::exception& e){
  std::cerr<<"FAIL: "<<e.what()<<"\n";
  return EXIT_FAILURE;
 }
}
