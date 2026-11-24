from pathlib import Path
import re
root=Path(__file__).resolve().parent
files=['Gradation.cpp','GradationPiPL.r','CMakeLists.txt','raster_core.hpp',
       'raster_avx2.hpp','blend_modes.hpp','blend_avx2.hpp','blend_raster.hpp',
       'stop_value_codec.hpp']
content={p:(root/p).read_text(encoding='utf-8-sig') for p in files}
plugin=content['Gradation.cpp']
pipl=content['GradationPiPL.r']
cmake=content['CMakeLists.txt']
# Official read-only AEGP Utility and Project suites are permitted from
# an Effect SDK PF_Cmd_EVENT; project depth is never guessed as 32bpc.
assert '#include "AE_GeneralPlug.h"' in plugin
usedAEGP=set(re.findall(r'\bAEGP_[a-zA-Z0-9_]+\b',plugin))
allowed={'AEGP_UtilitySuite6','AEGP_GetMainHWND',
         'AEGP_ProjSuite6','AEGP_ProjectH','AEGP_ProjBitDepth',
         'AEGP_ProjBitDepth_8','AEGP_ProjBitDepth_16','AEGP_ProjBitDepth_32',
         'AEGP_GetNumProjects','AEGP_GetProjectByIndex','AEGP_GetProjectBitDepth', 'AEGP_PersistentDataSuite4', 'AEGP_GetApplicationBlob', 'AEGP_PersistentBlobH', 'AEGP_DoesKeyExist', 'AEGP_GetFpLong', 'AEGP_PersistentType_MACHINE_INDEPENDENT'}
assert usedAEGP<=allowed,usedAEGP
assert 'AEGP_RegisterWithAEGP(' not in plugin
assert 'AEGP_SetProjectBitDepth' not in plugin
assert 'AEFX_SuiteScoper<AEGP_UtilitySuite6,true>' in plugin
assert 'AEFX_SuiteScoper<AEGP_ProjSuite6,true>' in plugin
assert 'getCurrentProjectDepth(in,out,activeDepth,depthFailure)' in plugin
assert 'stopui::editRGBA(' in plugin and '&onColorPickerPreview,&preview' in plugin
assert 'PF_InvalidateRect' in plugin and 'st=beforePicker' in plugin
assert 'stopui::editRGBA(owner,edited,stopui::Depth::Bpc32' not in plugin
assert 'getMainHWNDFromAE(in,out,hostFailure)' in plugin
assert 'GetWindowThreadProcessId(owner,&pid)' in plugin
assert 'extern "C" DllExport PF_Err EffectMain(' in plugin,'EffectMain missing'
assert 'PF_Cmd_GLOBAL_SETUP' in plugin and 'PF_Cmd_PARAMS_SETUP' in plugin
assert 'PF_Cmd_SMART_RENDER' in plugin and 'PF_Cmd_ARBITRARY_CALLBACK' in plugin
assert 'PF_Cmd_EVENT' in plugin and 'PF_PUI_CONTROL' in plugin
assert 'PF_Arbitrary_COPY_FUNC' in plugin and 'PF_GET_HANDLE_SIZE' in plugin
assert 'PF_ADD_PARAM(in_data,-1,&def)' in plugin
assert 'e->u.copy_func_params.dst_arbPH,value' in plugin
assert 'PF_OutFlag2_SUPPORTS_THREADED_RENDERING' in plugin
assert 'PF_OutFlag2_SUPPORTS_SMART_RENDER' in plugin
assert 'PF_OutFlag2_FLOAT_COLOR_AWARE' in plugin
assert 'AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS }' in pipl
assert 'AE_Effect_Version { 525828 }' in pipl,'Version mismatch'
assert '0x06008400' in pipl and '0x0A001480' in pipl,'PiPL flags mismatch'
assert 'Kind { AEEffect }' in pipl and 'CodeWin64X86 { "EffectMain" }' in pipl
# Custom dialog is invoked ONLY through official PF_Cmd_EVENT, while
# Windows DialogBoxIndirectParamW owns the native modal lifetime.
modal=(root/'picker_slugia/sdk_modal_picker.cpp').read_text(encoding='utf-8')
assert 'PF_Cmd_EVENT' in plugin and 'stopui::editRGBA(' in plugin
assert 'DialogBoxIndirectParamW(' in modal and 'EndDialog(' in modal
assert 'WM_START_PICKER' in modal and 'PostMessageW(h,WM_START_PICKER' in modal
assert 'PF_OutFlag_DISPLAY_ERROR_MESSAGE' in plugin
assert 'reportPickerFailure(out,hostFailure)' in plugin
assert 'reportPickerFailure(out,pickerFailure.c_str())' in plugin
assert 'bool open(HWND owner,' in modal and 's.open(owner,value,failureReason)' in modal
assert 'ownerThread!=GetCurrentThreadId()' in modal
assert 'case WM_DESTROY:\n    endWindowInput();' in modal
assert 'void cleanup() noexcept' in modal and 'renderer.reset();' in modal
assert modal.index('DialogBoxIndirectParamW(') < modal.index('cleanup();\n  if(error)std::rethrow_exception(error)')
for forbidden in ('EnableWindow','GetMessageW','CreateWindowExW','DestroyWindow','GetActiveWindow','GetForegroundWindow'):
 assert not re.search(r'(?m)^(?!\s*//)[^\n]*\b'+forbidden+r'\s*\(',modal), 'Manual modal window ownership forbidden: '+forbidden
assert not re.search(r'\b(?:AEGP_|WriteProcessMemory|CreateRemoteThread)\w*\b',modal), 'Non-effect API or host injection forbidden'
assert 'picker_slugia/sdk_modal_picker.cpp' in cmake and 'slugia::slugia' in cmake
# One canonical Slugia checkout for ALL Gradation builds. The bundled old
# third_party clone must never supply active sources to CMake.
assert 'GRADATION_SLUGIA_SOURCE' in cmake
assert 'add_subdirectory("${GRADATION_SLUGIA_SOURCE}"' in cmake
assert 'add_subdirectory("third_party/slugia"' not in cmake
# Validate the actual attached source designs rather than allowing the
# hardcoded prior-v5 geometry to silently return.
picker=(root/'picker_slugia/ColorPicker.slugui').read_text(encoding='utf-8')
assert picker == (root/'picker_slugia/ColorPicker-GUI(3).slugui').read_text(encoding='utf-8'), (
  'The canonical picker must match the latest complete user Figma file')
assert 'width: 797px;' in picker and 'height: 444px;' in picker
assert 'Rectangle Frame_6_42_95' in picker
button=(root/'picker_slugia/Button-State.slugui').read_text(encoding='utf-8')
scene=(root/'picker_slugia/figma_scene.cpp').read_text(encoding='utf-8')
ui=(root/'picker_slugia/picker_ui.cpp').read_text(encoding='utf-8')
for value in ('// exporter-revision: 2026-11-22-stroke-v6',
              'component Color_PickerGenerated',
              'fill-layers: [#ffffff, linear(#ff0000, #ff000000',
              'linear-stops([stop(0, #ff0000)', 'Rectangle Frame_6_42_95',
              'Rectangle Rectangle_3_10_6', 'Rectangle Rectangle_1_1_169',
              'Shape Handle_1_44'):
 assert value in picker, 'New ColorPicker source missing: '+value
assert re.search(r'Rectangle Rectangle_1_1_169\s*\{\s*x: 0px;\s*y: 48.5px;',picker)
assert '400.f+388.f*(pos.x-1.f)' in scene
assert 'originalFrame' in scene and 'originalSlider' in scene
assert 'setPaintFast("Rectangle_3_10_6"' in scene
assert 'setPaintFast("Rectangle_1_1_169"' in scene
assert 'GetForegroundWindow(' not in modal and 'GetActiveWindow(' not in modal
# Button-State's first-party Figma Spectrum variants constrain ALL states.
for value in ('State_Default','State_Hover','State_Touch',
              'State_Focus','State_Disabled','Checkbox_Selected_31_1257',
              'Checkbox_Selected_31_1281','Checkbox_Selected_31_1359',
              'Rectangle Rectangle_4_31_165','Rectangle Focus_Ring_31_1237',
              'stroke: #4096f3;', 'stroke: #d0d0d0;',
              'fill: #0078d7;'):
 assert value in button, 'Missing Figma state source: '+value
assert 'stateStroke(WidgetState s)' in scene and 'visualState(' in ui
assert 'state.radioStates[' in ui and 'state.hexFocused' in ui
assert 'PiPLtool.exe' in cmake and 'regenerate_pipl.py' in cmake
assert 'GradationPiPL.rc' in cmake and 'GradationPiPL.r' in cmake
assert 'add_library(Gradation MODULE Gradation.cpp GradationPiPL.rc)' not in cmake
for s in content.values():
 assert not re.search(r'\b(?:DetourAttach|MinHook|MH_CreateHook|WriteProcessMemory|CreateRemoteThread)\b',s),'Forbidden hook/injection'
print('Effect SDK + read-only AEGP ProjectSuite6/UtilitySuite6 + OS modal audit PASS: actual project depth, owner validation, deferred teardown, no host hooks')
