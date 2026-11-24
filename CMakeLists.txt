cmake_minimum_required(VERSION 3.22)
project(Gradation LANGUAGES CXX)
option(GRADATION_CORE_ONLY "Build portable CPU tests without Adobe SDK or GPU/UI dependencies" OFF)
if(GRADATION_CORE_ONLY)
 include(cmake/PortableTests.cmake)
 option(GRADATION_TEST_METAL "Build standalone Metal parity test" OFF)
 if(APPLE AND GRADATION_TEST_METAL)
  enable_language(OBJCXX)
  include(cmake/MetalLibrary.cmake)
 endif()
 return()
endif()
if(APPLE)
 enable_language(OBJCXX)
 include(cmake/MacOS.cmake)
 return()
endif()
if(NOT WIN32)
 message(FATAL_ERROR "Use -DGRADATION_CORE_ONLY=ON for a standalone CPU test build")
endif()
enable_language(RC)
set(AE_SDK_ROOT "D:/Developer/TrueDither" CACHE PATH "Adobe AE SDK Examples directory (Headers, Util, Resources)")
set(GRADATION_SLUGIA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../slugia" CACHE PATH "Existing Slugia source checkout")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Rebuild the embedded PiPL from its canonical Adobe SDK source.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(GRADATION_PIPL_RC "${CMAKE_CURRENT_BINARY_DIR}/pipl/GradationPiPL.rc")
add_custom_command(
 OUTPUT "${GRADATION_PIPL_RC}"
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/regenerate_pipl.py"
         "${CMAKE_CURRENT_SOURCE_DIR}/GradationPiPL.r" "${GRADATION_PIPL_RC}"
         "${AE_SDK_ROOT}/Headers" "${AE_SDK_ROOT}/Resources/PiPLtool.exe" "${CMAKE_CXX_COMPILER}"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/GradationPiPL.r"
         "${CMAKE_CURRENT_SOURCE_DIR}/regenerate_pipl.py"
         "${AE_SDK_ROOT}/Headers/AEConfig.h"
         "${AE_SDK_ROOT}/Headers/AE_EffectVers.h"
 VERBATIM
)
set_source_files_properties("${GRADATION_PIPL_RC}" PROPERTIES GENERATED TRUE)
add_library(Gradation MODULE Gradation.cpp "${GRADATION_PIPL_RC}")
# Offline CUDA compilation, no CUDA runtime dependency for AE startup.
find_program(GRADATION_NVCC NAMES nvcc REQUIRED HINTS "$ENV{CUDA_PATH}/bin")
set(GRADATION_CUDA_BIN "${CMAKE_CURRENT_BINARY_DIR}/gradation_cuda.fatbin")
set(GRADATION_CUDA_HEADER "${CMAKE_CURRENT_BINARY_DIR}/gradation_cuda_blob.hpp")
add_custom_command(OUTPUT "${GRADATION_CUDA_HEADER}"
 BYPRODUCTS "${GRADATION_CUDA_BIN}"
 COMMAND "${GRADATION_NVCC}" --fatbin --std=c++17 --fmad=false
  -gencode arch=compute_75,code=sm_75 -gencode arch=compute_75,code=compute_75
  "${CMAKE_CURRENT_SOURCE_DIR}/gradation_cuda.cu" -o "${GRADATION_CUDA_BIN}"
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_cuda.py"
  "${GRADATION_CUDA_BIN}" "${GRADATION_CUDA_HEADER}"
 DEPENDS gradation_cuda.cu gradation_cuda_params.hpp embed_cuda.py
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
# Single canonical Slugia source: D:/Developer/slugia, synced to
# ken-eizo/slugia main. Never build or edit the obsolete nested clone.
# GRADATION_SLUGIA_SOURCE is configurable above.
if(NOT EXISTS "${GRADATION_SLUGIA_SOURCE}/CMakeLists.txt")
  message(FATAL_ERROR "Set GRADATION_SLUGIA_SOURCE to the existing Slugia checkout")
endif()
add_subdirectory("${GRADATION_SLUGIA_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/slugia" EXCLUDE_FROM_ALL)
set(GRADATION_FIGMA_GENERATED
    "${CMAKE_CURRENT_BINARY_DIR}/picker_generated/color_picker.generated.hpp")
slugia_compile_slugui(
  INPUT "${CMAKE_CURRENT_SOURCE_DIR}/picker_slugia/ColorPicker.slugui"
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
  INPUT "${CMAKE_CURRENT_SOURCE_DIR}/picker_slugia/Button-State.slugui"
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
  picker_slugia/cursors.rc
  picker_slugia/sdk_modal_picker.cpp
  picker_slugia/figma_scene.cpp
  picker_slugia/picker_ui.cpp)
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
  set_source_files_properties(picker_slugia/figma_scene.cpp
    PROPERTIES COMPILE_OPTIONS "/O1;/Ob2;/bigobj")
endif()
# The Figma AOT bridge must rebuild when the original .slugui changes.
set_property(SOURCE picker_slugia/figma_scene.cpp APPEND PROPERTY
 OBJECT_DEPENDS "${GRADATION_FIGMA_GENERATED}")
set_property(SOURCE picker_slugia/figma_scene.cpp APPEND PROPERTY
 OBJECT_DEPENDS "${GRADATION_BUTTON_STATES_GENERATED}")


# One canonical build output only: dist/Release/Gradation.aex (the
# project-level output directory set above). NEVER install to Adobe's
# Plug-ins directory automatically; AE host verification is user-run.
# Diagnostic process is NOT deployed to AE or dist. Tests Windows
# DialogBoxIndirectParamW's actual owner enable/disable lifetime.
add_executable(gradation_sdk_modal_lifetime_test EXCLUDE_FROM_ALL
  picker_slugia/modal_lifetime_test.cpp
  picker_slugia/sdk_modal_picker.cpp
  picker_slugia/figma_scene.cpp
  picker_slugia/picker_ui.cpp)
add_dependencies(gradation_sdk_modal_lifetime_test gradation_embedded_picker_codegen)
target_include_directories(gradation_sdk_modal_lifetime_test PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/picker_generated")
target_compile_features(gradation_sdk_modal_lifetime_test PRIVATE cxx_std_20)
target_compile_definitions(gradation_sdk_modal_lifetime_test PRIVATE NOMINMAX)
if(MSVC)
  target_compile_options(gradation_sdk_modal_lifetime_test PRIVATE /utf-8)
endif()
target_link_libraries(gradation_sdk_modal_lifetime_test PRIVATE
  slugia::slugia user32 gdi32 dwmapi delayimp)
if(MSVC)
  target_link_options(gradation_sdk_modal_lifetime_test PRIVATE
    "/DELAYLOAD:vulkan-1.dll" "/OPT:REF" "/OPT:ICF" "/INCREMENTAL:NO")
  set_source_files_properties(picker_slugia/figma_scene.cpp PROPERTIES
    COMPILE_OPTIONS "/O1;/Ob2;/bigobj")
endif()

# Invisible native dialog keyboard routing test. It creates a hidden HWND
# but does not show a window, start After Effects or initialize Vulkan.
# Critically tests DefDlgProc's DWLP_MSGRESULT contract, not just codepoints.
add_executable(gradation_picker_native_keyboard_headless
  picker_slugia/native_keyboard_headless_test.cpp
  picker_slugia/picker_ui.cpp)
target_compile_features(gradation_picker_native_keyboard_headless PRIVATE cxx_std_20)
target_link_libraries(gradation_picker_native_keyboard_headless PRIVATE slugia::slugia user32 gdi32)
if(MSVC)
  target_compile_options(gradation_picker_native_keyboard_headless PRIVATE /utf-8)
endif()
add_test(NAME gradation_picker_native_keyboard_headless
  COMMAND gradation_picker_native_keyboard_headless)

# Native Adobe Drawbot Custom UI geometry/theme/physical-screen-fraction test.
# No Adobe host, window, Slugia runtime, or GPU initialization.
add_executable(gradation_custom_ui_layout_tests
  gradient_custom_ui_layout_tests.cpp)
target_compile_features(gradation_custom_ui_layout_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(gradation_custom_ui_layout_tests PRIVATE /utf-8)
endif()
add_test(NAME gradation_custom_ui_layout_tests
  COMMAND gradation_custom_ui_layout_tests)

# Pure CPU/AOT source fidelity test: never opens a window or initializes
# Vulkan, and verifies exact Figma dimensions/fills and state variants.
add_executable(gradation_slugui_source_fidelity_tests
  picker_slugia/source_fidelity_tests.cpp
  picker_slugia/figma_scene.cpp
  picker_slugia/picker_ui.cpp)
add_dependencies(gradation_slugui_source_fidelity_tests
  gradation_embedded_picker_codegen)
target_include_directories(gradation_slugui_source_fidelity_tests PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/picker_generated")
target_compile_features(gradation_slugui_source_fidelity_tests PRIVATE cxx_std_20)
target_link_libraries(gradation_slugui_source_fidelity_tests PRIVATE slugia::slugia)
if(MSVC)
  target_compile_options(gradation_slugui_source_fidelity_tests PRIVATE /utf-8)
endif()
add_test(NAME gradation_slugui_source_fidelity_tests
  COMMAND gradation_slugui_source_fidelity_tests)

# Only this explicit --headless diagnostic is registered with CTest.
# The GUI lifetime executable's normal mode is NEVER invoked by CI.
add_test(NAME gradation_picker_null_owner_headless
  COMMAND gradation_sdk_modal_lifetime_test --headless)

add_executable(gradation_core_tests core_tests.cpp)
target_include_directories(gradation_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
enable_testing()
add_test(NAME gradation_core_tests COMMAND gradation_core_tests)

add_executable(gradation_color_tests color_tests.cpp)
add_test(NAME gradation_color_tests COMMAND gradation_color_tests)

add_executable(gradation_benchmark benchmark.cpp)

add_executable(gradation_spectral_reference_tests spectral_reference_tests.cpp)
add_test(NAME gradation_spectral_reference_tests COMMAND gradation_spectral_reference_tests)
set_tests_properties(gradation_spectral_reference_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(gradation_raster_tests raster_tests.cpp)
add_test(NAME gradation_raster_tests COMMAND gradation_raster_tests)

add_executable(gradation_benchmark_raster benchmark_raster.cpp)

# AE 2025/2026/2027 all require AVX2 on Windows x64.
if(MSVC)
 foreach(tgt Gradation gradation_core_tests gradation_color_tests
             gradation_spectral_reference_tests gradation_raster_tests
             gradation_benchmark gradation_benchmark_raster)
  target_compile_options(${tgt} PRIVATE /arch:AVX2)
 endforeach()
endif()

add_executable(gradation_concurrent_tests concurrent_raster_tests.cpp)
add_test(NAME gradation_concurrent_tests COMMAND gradation_concurrent_tests)

add_executable(gradation_precision_tests precision_raster_tests.cpp)
add_test(NAME gradation_precision_tests COMMAND gradation_precision_tests)
if(MSVC)
 target_compile_options(gradation_precision_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_stability_tests stability_tests.cpp)
add_test(NAME gradation_stability_tests COMMAND gradation_stability_tests)
if(MSVC)
 target_compile_options(gradation_stability_tests PRIVATE /arch:AVX2)
endif()
add_executable(gradation_precision_scalar_tests precision_raster_tests.cpp)
add_test(NAME gradation_precision_scalar_tests COMMAND gradation_precision_scalar_tests)

add_executable(gradation_spectral_extended_tests spectral_extended_tests.cpp)
add_test(NAME gradation_spectral_extended_tests COMMAND gradation_spectral_extended_tests)
set_tests_properties(gradation_spectral_extended_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(gradation_benchmark_translucent benchmark_translucent.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_translucent PRIVATE /arch:AVX2)
endif()
add_executable(gradation_benchmark_translucent_scalar benchmark_translucent.cpp)

add_executable(gradation_roi_tests roi_tests.cpp roi_scalar_reference.cpp)
set_source_files_properties(roi_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
add_test(NAME gradation_roi_tests COMMAND gradation_roi_tests)

add_executable(gradation_blend_tests blend_tests.cpp)
add_test(NAME gradation_blend_tests COMMAND gradation_blend_tests)
if(MSVC)
 target_compile_options(gradation_blend_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_stop_codec_tests stop_codec_tests.cpp)
add_test(NAME gradation_stop_codec_tests COMMAND gradation_stop_codec_tests)

add_executable(gradation_benchmark_blend benchmark_blend.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_blend PRIVATE /arch:AVX2)
endif()

add_executable(gradation_empty_source_tests empty_source_tests.cpp)
add_test(NAME gradation_empty_source_tests COMMAND gradation_empty_source_tests)
if(MSVC)
 target_compile_options(gradation_empty_source_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_blend_avx2_tests blend_avx2_tests.cpp blend_scalar_reference.cpp)
set_source_files_properties(blend_avx2_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
add_test(NAME gradation_blend_avx2_tests COMMAND gradation_blend_avx2_tests)

add_executable(gradation_blend_stability_tests blend_stability_tests.cpp)
add_test(NAME gradation_blend_stability_tests COMMAND gradation_blend_stability_tests)
if(MSVC)
 target_compile_options(gradation_blend_stability_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_startup_host_tests startup_host_tests.cpp)
get_filename_component(GRADATION_CUDA_BIN_DIR "${GRADATION_NVCC}" DIRECTORY)
get_filename_component(GRADATION_CUDA_ROOT "${GRADATION_CUDA_BIN_DIR}" DIRECTORY)
target_include_directories(gradation_startup_host_tests PRIVATE "${GRADATION_CUDA_ROOT}/include")
target_link_libraries(gradation_startup_host_tests PRIVATE "${GRADATION_CUDA_ROOT}/lib/x64/cuda.lib")
target_link_options(gradation_startup_host_tests PRIVATE "/DELAYLOAD:nvcuda.dll")
target_link_libraries(gradation_startup_host_tests PRIVATE delayimp)

target_include_directories(gradation_startup_host_tests PRIVATE
  "${AE_SDK_ROOT}/Headers" "${AE_SDK_ROOT}/Headers/SP")
add_test(NAME gradation_startup_host_tests COMMAND gradation_startup_host_tests "$<TARGET_FILE:Gradation>")

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
 add_test(NAME gradation_effect_only_sdk_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/audit_effect_only.py")
endif()

add_test(NAME gradation_pipl_binary_contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/inspect_pipl_binary.py" "$<TARGET_FILE:Gradation>")

add_executable(gradation_blend_roi_avx2_tests blend_roi_avx2_tests.cpp blend_scalar_reference.cpp)
set_source_files_properties(blend_roi_avx2_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
add_test(NAME gradation_blend_roi_avx2_tests COMMAND gradation_blend_roi_avx2_tests)

add_executable(gradation_benchmark_cropped_avx2 benchmark_cropped.cpp)
add_executable(gradation_benchmark_cropped_scalar benchmark_cropped.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_cropped_avx2 PRIVATE /arch:AVX2)
endif()

add_executable(gradation_benchmark_generator benchmark_generator.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_generator PRIVATE /arch:AVX2)
endif()

add_executable(gradation_lut_cache_tests lut_cache_tests.cpp)
add_test(NAME gradation_lut_cache_tests COMMAND gradation_lut_cache_tests)
if(MSVC)
 target_compile_options(gradation_lut_cache_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_stop_picker_tests stop_picker_tests.cpp)
target_link_libraries(gradation_stop_picker_tests PRIVATE user32 gdi32 comdlg32)
add_test(NAME gradation_stop_picker_tests COMMAND gradation_stop_picker_tests)

add_executable(gradation_spatial_reuse_tests spatial_reuse_tests.cpp roi_scalar_reference.cpp blend_scalar_reference.cpp)
set_source_files_properties(spatial_reuse_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
add_test(NAME gradation_spatial_reuse_tests COMMAND gradation_spatial_reuse_tests)

add_executable(gradation_benchmark_mapping_rows benchmark_mapping_rows.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_mapping_rows PRIVATE /arch:AVX2)
endif()

add_executable(gradation_angle_approx_tests angle_approx_tests.cpp)
if(MSVC)
 target_compile_options(gradation_angle_approx_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_angle_approx_tests COMMAND gradation_angle_approx_tests)

add_executable(gradation_benchmark_tiles benchmark_tiles.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_tiles PRIVATE /arch:AVX2)
endif()

add_executable(gradation_direct_srgb32_tests direct_srgb32_tests.cpp)
if(MSVC)
 target_compile_options(gradation_direct_srgb32_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_direct_srgb32_tests COMMAND gradation_direct_srgb32_tests)

add_executable(gradation_benchmark_direct_srgb32 benchmark_direct_srgb32.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_direct_srgb32 PRIVATE /arch:AVX2)
endif()

add_executable(gradation_direct_srgb16_tests direct_srgb16_tests.cpp)
if(MSVC)
 target_compile_options(gradation_direct_srgb16_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_direct_srgb16_tests COMMAND gradation_direct_srgb16_tests)
add_executable(gradation_benchmark_direct_srgb16 benchmark_direct_srgb16.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_direct_srgb16 PRIVATE /arch:AVX2)
endif()

add_executable(gradation_direct_srgb8_tests direct_srgb8_tests.cpp)
if(MSVC)
 target_compile_options(gradation_direct_srgb8_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_direct_srgb8_tests COMMAND gradation_direct_srgb8_tests)
add_executable(gradation_benchmark_direct_srgb8 benchmark_direct_srgb8.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_direct_srgb8 PRIVATE /arch:AVX2)
endif()

add_executable(gradation_flat_gradient_rows_tests flat_gradient_rows_tests.cpp blend_scalar_reference.cpp)
if(MSVC)
 set_source_files_properties(flat_gradient_rows_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
endif()
add_test(NAME gradation_flat_gradient_rows_tests COMMAND gradation_flat_gradient_rows_tests)

add_executable(gradation_benchmark_flat_optimized benchmark_flat_rows.cpp)
add_executable(gradation_benchmark_flat_baseline benchmark_flat_rows.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_flat_optimized PRIVATE /arch:AVX2)
 target_compile_options(gradation_benchmark_flat_baseline PRIVATE /arch:AVX2)
endif()
target_compile_definitions(gradation_benchmark_flat_baseline PRIVATE GRADATION_DISABLE_FLAT_ROWS)
add_executable(gradation_benchmark_flat_alwaysfill benchmark_flat_rows.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_flat_alwaysfill PRIVATE /arch:AVX2)
endif()
target_compile_definitions(gradation_benchmark_flat_alwaysfill PRIVATE GRADATION_FLAT_ALWAYS_FILL)

add_executable(gradation_multi_depth_mfr_stress_tests multi_depth_mfr_stress_tests.cpp)
if(MSVC)
 target_compile_options(gradation_multi_depth_mfr_stress_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_multi_depth_mfr_stress_tests COMMAND gradation_multi_depth_mfr_stress_tests)

add_executable(gradation_benchmark_normal_mfr_tiles benchmark_normal_mfr_tiles.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_normal_mfr_tiles PRIVATE /arch:AVX2)
endif()

add_executable(gradation_stop_interpolation_tests stop_interpolation_tests.cpp)
if(MSVC)
 target_compile_options(gradation_stop_interpolation_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_stop_interpolation_tests COMMAND gradation_stop_interpolation_tests)

add_executable(gradation_opaque_normal_equivalence_tests opaque_normal_generator_equivalence_tests.cpp)
if(MSVC)
 target_compile_options(gradation_opaque_normal_equivalence_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_opaque_normal_equivalence_tests COMMAND gradation_opaque_normal_equivalence_tests)

add_executable(gradation_benchmark_f32_alpha_load benchmark_f32_alpha_load.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_f32_alpha_load PRIVATE /arch:AVX2)
endif()

add_executable(gradation_benchmark_direct_srgb32_gather benchmark_direct_srgb32.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_direct_srgb32_gather PRIVATE /arch:AVX2)
endif()
target_compile_definitions(gradation_benchmark_direct_srgb32_gather PRIVATE GRADATION_MAPPING_GATHER_SOURCE)

add_executable(gradation_benchmark_f32_parallel_rows benchmark_f32_parallel_rows.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_f32_parallel_rows PRIVATE /arch:AVX2)
endif()

add_executable(gradation_raster_parallel_tests raster_parallel_tests.cpp)
if(MSVC)
 target_compile_options(gradation_raster_parallel_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_raster_parallel_tests COMMAND gradation_raster_parallel_tests)

add_executable(gradation_benchmark_f32_adaptive benchmark_f32_adaptive.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_f32_adaptive PRIVATE /arch:AVX2)
endif()

add_executable(gradation_uniform_gradient_fold_tests uniform_gradient_fold_tests.cpp)
if(MSVC)
 target_compile_options(gradation_uniform_gradient_fold_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_uniform_gradient_fold_tests COMMAND gradation_uniform_gradient_fold_tests)

add_executable(gradation_direct_srgb3_tests direct_srgb3_tests.cpp)
if(MSVC)
 target_compile_options(gradation_direct_srgb3_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_direct_srgb3_tests COMMAND gradation_direct_srgb3_tests)

add_executable(gradation_benchmark_direct_srgb3 benchmark_direct_srgb3.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_direct_srgb3 PRIVATE /arch:AVX2)
endif()

add_executable(gradation_benchmark_direct_srgb3_generic benchmark_direct_srgb3.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_direct_srgb3_generic PRIVATE /arch:AVX2)
endif()
target_compile_definitions(gradation_benchmark_direct_srgb3_generic PRIVATE GRADATION_DISABLE_FAST_NORMAL_MULTIPLY)

add_executable(gradation_lazy_f32_lut_tests lazy_f32_lut_tests.cpp)
if(MSVC)
 target_compile_options(gradation_lazy_f32_lut_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_lazy_f32_lut_tests COMMAND gradation_lazy_f32_lut_tests)

add_executable(gradation_benchmark_lazy_lut_cache benchmark_lazy_lut_cache.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_lazy_lut_cache PRIVATE /arch:AVX2)
endif()

add_executable(gradation_lazy_mfr_cache_tests lazy_mfr_cache_tests.cpp)
if(MSVC)
 target_compile_options(gradation_lazy_mfr_cache_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_lazy_mfr_cache_tests COMMAND gradation_lazy_mfr_cache_tests)

add_executable(gradation_benchmark_colorspace_precompute benchmark_color_space_precompute.cpp)
add_executable(gradation_benchmark_colorspace_baseline benchmark_color_space_precompute.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_colorspace_precompute PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_benchmark_colorspace_baseline PRIVATE /arch:AVX2 /fp:precise)
endif()
target_compile_definitions(gradation_benchmark_colorspace_baseline PRIVATE GRADATION_DISABLE_COLOR_PRECONVERT)

add_executable(gradation_benchmark_source16_sequential benchmark_source16_blend.cpp)
add_executable(gradation_benchmark_source16_gather benchmark_source16_blend.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_source16_sequential PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_benchmark_source16_gather PRIVATE /arch:AVX2 /fp:precise)
endif()
target_compile_definitions(gradation_benchmark_source16_gather PRIVATE GRADATION_SOURCE16_GATHER_BASELINE)

add_executable(gradation_benchmark_f32_store_old benchmark_direct_srgb32.cpp)
add_executable(gradation_benchmark_f32_store_avx2 benchmark_direct_srgb32.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_f32_store_old PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_benchmark_f32_store_avx2 PRIVATE /arch:AVX2 /fp:precise)
endif()
target_compile_definitions(gradation_benchmark_f32_store_old PRIVATE GRADATION_F32_OLD_TRANSPOSE)

add_executable(gradation_benchmark_simd_io_micro benchmark_simd_io_micro.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_simd_io_micro PRIVATE /arch:AVX2 /fp:precise)
endif()

add_executable(gradation_transparent_alpha0_tests transparent_alpha0_tests.cpp)
if(MSVC)
 target_compile_options(gradation_transparent_alpha0_tests PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_transparent_alpha0_tests COMMAND gradation_transparent_alpha0_tests)

add_executable(gradation_horizontal32_tests horizontal32_tests.cpp)
if(MSVC)
 target_compile_options(gradation_horizontal32_tests PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_horizontal32_tests COMMAND gradation_horizontal32_tests)

add_executable(gradation_benchmark_default_gray benchmark_default_srgb32.cpp)
add_executable(gradation_benchmark_default_no_gray benchmark_default_srgb32.cpp)
if(MSVC)
 target_compile_options(gradation_benchmark_default_gray PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_benchmark_default_no_gray PRIVATE /arch:AVX2 /fp:precise)
endif()
target_compile_definitions(gradation_benchmark_default_no_gray PRIVATE GRADATION_DISABLE_F32_GRAY_FAST)

add_executable(gradation_gray32_hash_base gray32_abi_hash_tests.cpp)
add_executable(gradation_gray32_hash_fast gray32_abi_hash_tests.cpp)
if(MSVC)
 target_compile_options(gradation_gray32_hash_base PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_gray32_hash_fast PRIVATE /arch:AVX2 /fp:precise)
endif()
target_compile_definitions(gradation_gray32_hash_base PRIVATE GRADATION_DISABLE_F32_GRAY_FAST)
add_test(NAME gradation_gray32_exact_parity
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_gray32_abi_hash.py"
 $<TARGET_FILE:gradation_gray32_hash_base>
 $<TARGET_FILE:gradation_gray32_hash_fast>)

# Historical preview depends on the removed legacy dialog. Keep its source,
# but never make it a required target of the embedded-picker build.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stop_rgba_dialog_win.hpp")
 add_executable(gradation_picker_preview WIN32 EXCLUDE_FROM_ALL picker_preview_win.cpp)
 target_link_libraries(gradation_picker_preview PRIVATE user32 gdi32)
endif()

add_executable(gradation_integer_row_cache_tests integer_row_cache_tests.cpp)
if(MSVC)
 target_compile_options(gradation_integer_row_cache_tests PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_integer_row_cache_tests COMMAND gradation_integer_row_cache_tests)

add_executable(gradation_vertical_reuse_tests vertical_reuse_tests.cpp)
add_executable(gradation_vertical_reuse_baseline vertical_reuse_tests.cpp)
target_compile_definitions(gradation_vertical_reuse_baseline PRIVATE GRADATION_DISABLE_VERTICAL_REUSE)
if(MSVC)
 target_compile_options(gradation_vertical_reuse_tests PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_vertical_reuse_baseline PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_vertical_reuse_tests COMMAND gradation_vertical_reuse_tests)

add_executable(gradation_mapping16_io_tests mapping16_io_tests.cpp)
add_executable(gradation_mapping16_io_baseline mapping16_io_tests.cpp)
target_compile_definitions(gradation_mapping16_io_baseline PRIVATE GRADATION_MAPPING16_GATHER_BASELINE)
if(MSVC)
 target_compile_options(gradation_mapping16_io_tests PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_mapping16_io_baseline PRIVATE /arch:AVX2 /fp:precise)
endif()

add_test(NAME gradation_mapping16_io_parity
 COMMAND "${CMAKE_COMMAND}"
 -DBASELINE=$<TARGET_FILE:gradation_mapping16_io_baseline>
 -DOPTIMIZED=$<TARGET_FILE:gradation_mapping16_io_tests>
 -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
 -P "${CMAKE_CURRENT_SOURCE_DIR}/mapping16_io_compare.cmake")

add_executable(gradation_float_lut_io_tests float_lut_io_tests.cpp)
add_executable(gradation_float_lut_io_baseline float_lut_io_tests.cpp)
target_compile_definitions(gradation_float_lut_io_baseline PRIVATE GRADATION_F32_LUT_GATHER_BASELINE)
if(MSVC)
 target_compile_options(gradation_float_lut_io_tests PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_float_lut_io_baseline PRIVATE /arch:AVX2 /fp:precise)
endif()

add_test(NAME gradation_float_lut_io_parity
 COMMAND "${CMAKE_COMMAND}"
 -DBASELINE=$<TARGET_FILE:gradation_float_lut_io_baseline>
 -DOPTIMIZED=$<TARGET_FILE:gradation_float_lut_io_tests>
 -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
 -P "${CMAKE_CURRENT_SOURCE_DIR}/float_lut_io_compare.cmake")

add_executable(gradation_angle_row_tests angle_row_tests.cpp)
add_executable(gradation_angle_row_baseline angle_row_tests.cpp)
target_compile_definitions(gradation_angle_row_baseline PRIVATE GRADATION_ANGLE_ROW_BASELINE)
if(MSVC)
 target_compile_options(gradation_angle_row_tests PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_angle_row_baseline PRIVATE /arch:AVX2 /fp:precise)
endif()

add_test(NAME gradation_angle_row_parity
 COMMAND "${CMAKE_COMMAND}"
 -DBASELINE=$<TARGET_FILE:gradation_angle_row_baseline>
 -DOPTIMIZED=$<TARGET_FILE:gradation_angle_row_tests>
 -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
 -P "${CMAKE_CURRENT_SOURCE_DIR}/angle_row_compare.cmake")

add_executable(gradation_angle_cache_tests angle_cache_tests.cpp)
if(MSVC)
 target_compile_options(gradation_angle_cache_tests PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_angle_cache_tests COMMAND gradation_angle_cache_tests)

add_test(NAME gradation_angle_cache_cached_store_tests COMMAND gradation_angle_cache_tests)
set_tests_properties(gradation_angle_cache_cached_store_tests PROPERTIES ENVIRONMENT "GRADATION_ANGLE_CACHE_STREAM=0")

add_executable(gradation_geometry_cache_tests geometry_cache_tests.cpp)
if(MSVC)
 target_compile_options(gradation_geometry_cache_tests PRIVATE /arch:AVX2 /fp:precise)
endif()

add_test(NAME gradation_geometry_cache_tests COMMAND gradation_geometry_cache_tests)

add_executable(gradation_ae_compute_cache_tests ae_compute_cache_tests.cpp)
target_include_directories(gradation_ae_compute_cache_tests PRIVATE
 "${AE_SDK_ROOT}/Headers" "${AE_SDK_ROOT}/Headers/SP")
target_compile_definitions(gradation_ae_compute_cache_tests PRIVATE MSWindows WIN32 _WINDOWS)
target_compile_options(gradation_ae_compute_cache_tests PRIVATE /arch:AVX2 /fp:precise)
add_test(NAME gradation_ae_compute_cache_tests COMMAND gradation_ae_compute_cache_tests)
add_executable(gradation_benchmark_cpu_matrix benchmark_cpu_matrix.cpp)
target_compile_options(gradation_benchmark_cpu_matrix PRIVATE /arch:AVX2 /fp:precise)
add_executable(gradation_jit_fill_experiment jit_fill_experiment.cpp)
target_compile_options(gradation_jit_fill_experiment PRIVATE /arch:AVX2 /fp:precise)

add_executable(gradation_benchmark_cpu_matrix_generic benchmark_cpu_matrix.cpp)
target_compile_options(gradation_benchmark_cpu_matrix_generic PRIVATE /arch:AVX2 /fp:precise)
target_compile_definitions(gradation_benchmark_cpu_matrix_generic PRIVATE GRADATION_GENERIC_BLEND_GRAPH)

add_executable(gradation_geometry_compression_experiment geometry_compression_experiment.cpp)
target_compile_options(gradation_geometry_compression_experiment PRIVATE /arch:AVX2 /fp:precise)

add_executable(gradation_blend_graph_tests blend_graph_tests.cpp)
target_compile_options(gradation_blend_graph_tests PRIVATE /arch:AVX2 /fp:precise)
add_test(NAME gradation_blend_graph_tests COMMAND gradation_blend_graph_tests)

# Opt-in packed blend LUT A/B experiment.
add_executable(gradation_benchmark_cpu_matrix_packed benchmark_cpu_matrix.cpp)
target_compile_definitions(gradation_benchmark_cpu_matrix_packed PRIVATE GRADATION_PACKED_BLEND_LUT)
if(MSVC)
 target_compile_options(gradation_benchmark_cpu_matrix_packed PRIVATE /arch:AVX2 /fp:precise)
endif()

add_executable(gradation_benchmark_cpu_matrix_legacy_math benchmark_cpu_matrix.cpp)
target_compile_definitions(gradation_benchmark_cpu_matrix_legacy_math PRIVATE GRADATION_LEGACY_NORMAL_FACTOR GRADATION_LEGACY_SEGMENTED_BLEND)
if(MSVC)
 target_compile_options(gradation_benchmark_cpu_matrix_legacy_math PRIVATE /arch:AVX2 /fp:precise)
endif()

add_executable(gradation_benchmark_cpu_matrix_no_axis benchmark_cpu_matrix.cpp)
target_compile_definitions(gradation_benchmark_cpu_matrix_no_axis PRIVATE GRADATION_DISABLE_COMPOSITE_AXIS_CACHE)
if(MSVC)
 target_compile_options(gradation_benchmark_cpu_matrix_no_axis PRIVATE /arch:AVX2 /fp:precise)
endif()

add_executable(gradation_benchmark_cpu_matrix_aos EXCLUDE_FROM_ALL benchmark_cpu_matrix.cpp)
target_compile_definitions(gradation_benchmark_cpu_matrix_aos PRIVATE GRADATION_EXPERIMENT_NORMAL_AOS)
target_compile_options(gradation_benchmark_cpu_matrix_aos PRIVATE /arch:AVX2 /fp:precise)
