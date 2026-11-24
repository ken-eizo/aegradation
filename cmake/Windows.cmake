enable_language(RC)
set(AE_SDK_ROOT "$ENV{AE_SDK_ROOT}" CACHE PATH "Adobe AE SDK Examples directory (Headers, Util, Resources)")
if(NOT EXISTS "${AE_SDK_ROOT}/Headers/AEConfig.h" OR NOT EXISTS "${AE_SDK_ROOT}/Resources/PiPLtool.exe")
 message(FATAL_ERROR "Set AE_SDK_ROOT to the Adobe SDK Examples directory")
endif()
set(GRADATION_SLUGIA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../slugia" CACHE PATH "Existing Slugia source checkout")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Rebuild the embedded PiPL from its canonical Adobe SDK source.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(GRADATION_PIPL_RC "${CMAKE_CURRENT_BINARY_DIR}/pipl/GradationPiPL.rc")
add_custom_command(
 OUTPUT "${GRADATION_PIPL_RC}"
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/regenerate_pipl.py"
         "${CMAKE_CURRENT_SOURCE_DIR}/assets/ae/GradationPiPL.r" "${GRADATION_PIPL_RC}"
         "${AE_SDK_ROOT}/Headers" "${AE_SDK_ROOT}/Resources/PiPLtool.exe" "${CMAKE_CXX_COMPILER}"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/ae/GradationPiPL.r"
         "${CMAKE_CURRENT_SOURCE_DIR}/tools/regenerate_pipl.py"
         "${AE_SDK_ROOT}/Headers/AEConfig.h"
         "${AE_SDK_ROOT}/Headers/AE_EffectVers.h"
 VERBATIM
)
set_source_files_properties("${GRADATION_PIPL_RC}" PROPERTIES GENERATED TRUE)
add_library(Gradation MODULE src/ae/Gradation.cpp "${GRADATION_PIPL_RC}")
# Offline CUDA compilation, no CUDA runtime dependency for AE startup.
find_program(GRADATION_NVCC NAMES nvcc REQUIRED HINTS "$ENV{CUDA_PATH}/bin")
set(GRADATION_CUDA_BIN "${CMAKE_CURRENT_BINARY_DIR}/gradation_cuda.fatbin")
set(GRADATION_CUDA_HEADER "${CMAKE_CURRENT_BINARY_DIR}/gradation_cuda_blob.hpp")
add_custom_command(OUTPUT "${GRADATION_CUDA_HEADER}"
 BYPRODUCTS "${GRADATION_CUDA_BIN}"
 COMMAND "${GRADATION_NVCC}" --fatbin --std=c++17 --fmad=false
  -gencode arch=compute_75,code=sm_75 -gencode arch=compute_75,code=compute_75
  "${CMAKE_CURRENT_SOURCE_DIR}/src/render/gpu/cuda/gradation_cuda.cu" -o "${GRADATION_CUDA_BIN}"
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_cuda.py"
  "${GRADATION_CUDA_BIN}" "${GRADATION_CUDA_HEADER}"
 DEPENDS src/render/gpu/cuda/gradation_cuda.cu src/render/gpu/shared/gradation_cuda_params.hpp tools/embed_cuda.py
 VERBATIM)
target_sources(Gradation PRIVATE "${GRADATION_CUDA_HEADER}")
target_include_directories(Gradation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_include_directories(Gradation PRIVATE
 "${AE_SDK_ROOT}/Headers"
 "${AE_SDK_ROOT}/Headers/SP"
 "${AE_SDK_ROOT}/Util")
target_compile_definitions(Gradation PRIVATE MSWindows WIN32 _WINDOWS PF_DEEP_COLOR_AWARE=1)
target_compile_options(Gradation PRIVATE /O2 /fp:precise /EHsc /W3)
target_link_libraries(Gradation PRIVATE user32 gdi32 comdlg32)
set_target_properties(Gradation PROPERTIES PREFIX "" SUFFIX ".aex"
 LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
# ---------------------------------------------------------------------------
# Single-AEX SDK-event-triggered, Win32-OS-managed modal: user-authored ColorPicker.slugui is compiled
# into Gradation.aex. No external EXE, no custom message pump, no manual EnableWindow.
# GPU initialization occurs only inside Windows DialogBoxIndirectParamW.
# ---------------------------------------------------------------------------
set(SLUGIA_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SLUGIA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SLUGIA_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SLUGIA_ENABLE_3D OFF CACHE BOOL "" FORCE)
set(SLUGIA_ENABLE_GLFW OFF CACHE BOOL "" FORCE)
set(SLUGIA_ENABLE_VALIDATION OFF CACHE BOOL "" FORCE)
set(SLUGIA_STATIC_MSVC_RUNTIME OFF CACHE BOOL "" FORCE)
set(SLUGIA_INSTALL OFF CACHE BOOL "" FORCE)
# Single external Slugia source checkout. Do not vendor a second copy.
# GRADATION_SLUGIA_SOURCE is configurable above.
if(NOT EXISTS "${GRADATION_SLUGIA_SOURCE}/CMakeLists.txt")
  message(FATAL_ERROR "Set GRADATION_SLUGIA_SOURCE to the existing Slugia checkout")
endif()
add_subdirectory("${GRADATION_SLUGIA_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/slugia" EXCLUDE_FROM_ALL)
set(GRADATION_FIGMA_GENERATED
    "${CMAKE_CURRENT_BINARY_DIR}/picker_generated/color_picker.generated.hpp")
slugia_compile_slugui(
  INPUT "${CMAKE_CURRENT_SOURCE_DIR}/assets/figma/ColorPicker.slugui"
  OUTPUT "${GRADATION_FIGMA_GENERATED}"
  NAMESPACE gradation::figma
  CLASS_NAME ColorPickerGenerated
  STRIP_SOURCE_METADATA
  STAMPED
)
# The state reference comes from the user's uploaded Button-State.slugui,
# not manually recreated Win32/GDI controls.
set(GRADATION_BUTTON_STATES_GENERATED
    "${CMAKE_CURRENT_BINARY_DIR}/picker_generated/button_state.generated.hpp")
slugia_compile_slugui(
  INPUT "${CMAKE_CURRENT_SOURCE_DIR}/assets/figma/Button-State.slugui"
  OUTPUT "${GRADATION_BUTTON_STATES_GENERATED}"
  NAMESPACE gradation::figma
  CLASS_NAME ButtonStateGenerated
  STRIP_SOURCE_METADATA
  STAMPED
)
add_custom_target(gradation_embedded_picker_codegen
  DEPENDS "${GRADATION_FIGMA_GENERATED}.stamp"
          "${GRADATION_BUTTON_STATES_GENERATED}.stamp"
)
target_sources(Gradation PRIVATE
  assets/windows/cursors.rc
  src/ui/windows/sdk_modal_picker.cpp
  src/ui/windows/figma_scene.cpp
  src/ui/windows/picker_ui.cpp)
add_dependencies(Gradation gradation_embedded_picker_codegen)
target_include_directories(Gradation PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/picker_generated")
target_compile_features(Gradation PRIVATE cxx_std_20)
target_link_libraries(Gradation PRIVATE
  slugia::slugia dwmapi)
# Dialog modal is the only owner of GPU; AE render callbacks never initialize Vulkan.
# Vulkan DLL uses the loader only when the user opens the color dialog.
if(MSVC)
  target_link_libraries(Gradation PRIVATE delayimp)
  target_link_options(Gradation PRIVATE
    "/DELAYLOAD:vulkan-1.dll" "/OPT:REF" "/OPT:ICF"
    "/INCREMENTAL:NO")
  target_compile_options(Gradation PRIVATE /Gy /Gw /utf-8)
  set_source_files_properties(src/ui/windows/figma_scene.cpp
    PROPERTIES COMPILE_OPTIONS "/O1;/Ob2;/bigobj")
endif()
# The Figma AOT bridge must rebuild when the original .slugui changes.
set_property(SOURCE src/ui/windows/figma_scene.cpp APPEND PROPERTY
 OBJECT_DEPENDS "${GRADATION_FIGMA_GENERATED}")
set_property(SOURCE src/ui/windows/figma_scene.cpp APPEND PROPERTY
 OBJECT_DEPENDS "${GRADATION_BUTTON_STATES_GENERATED}")


# One canonical build output only: dist/Release/Gradation.aex (the
# project-level output directory set above). NEVER install to Adobe's
# Plug-ins directory automatically; AE host verification is user-run.

include(cmake/WindowsTests.cmake)
include(cmake/Benchmarks.cmake)
