# Diagnostic process is NOT deployed to AE or dist. Tests Windows
# DialogBoxIndirectParamW's actual owner enable/disable lifetime.
add_executable(gradation_sdk_modal_lifetime_test
  tests/ui/modal_lifetime_test.cpp
  src/ui/windows/sdk_modal_picker.cpp
  src/ui/windows/figma_scene.cpp
  src/ui/windows/picker_ui.cpp)
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
  set_source_files_properties(src/ui/windows/figma_scene.cpp PROPERTIES
    COMPILE_OPTIONS "/O1;/Ob2;/bigobj")
endif()

# Invisible native dialog keyboard routing test. It creates a hidden HWND
# but does not show a window, start After Effects or initialize Vulkan.
# Critically tests DefDlgProc's DWLP_MSGRESULT contract, not just codepoints.
add_executable(gradation_picker_native_keyboard_headless
  tests/ui/native_keyboard_headless_test.cpp
  src/ui/windows/picker_ui.cpp)
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
  tests/ui/gradient_custom_ui_layout_tests.cpp)
target_compile_features(gradation_custom_ui_layout_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(gradation_custom_ui_layout_tests PRIVATE /utf-8)
endif()
add_test(NAME gradation_custom_ui_layout_tests
  COMMAND gradation_custom_ui_layout_tests)

# Pure CPU/AOT source fidelity test: never opens a window or initializes
# Vulkan, and verifies exact Figma dimensions/fills and state variants.
add_executable(gradation_slugui_source_fidelity_tests
  tests/ui/source_fidelity_tests.cpp
  src/ui/windows/figma_scene.cpp
  src/ui/windows/picker_ui.cpp)
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

add_executable(gradation_core_tests tests/core/core_tests.cpp)
target_include_directories(gradation_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
enable_testing()
add_test(NAME gradation_core_tests COMMAND gradation_core_tests)

add_executable(gradation_color_tests tests/core/color_tests.cpp)
add_test(NAME gradation_color_tests COMMAND gradation_color_tests)


add_executable(gradation_spectral_reference_tests tests/core/spectral_reference_tests.cpp)
add_test(NAME gradation_spectral_reference_tests COMMAND gradation_spectral_reference_tests)
set_tests_properties(gradation_spectral_reference_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(gradation_raster_tests tests/render/raster_tests.cpp)
add_test(NAME gradation_raster_tests COMMAND gradation_raster_tests)


# AE 2025/2026/2027 all require AVX2 on Windows x64.
if(MSVC)
 foreach(tgt Gradation gradation_core_tests gradation_color_tests
             gradation_spectral_reference_tests gradation_raster_tests)
  target_compile_options(${tgt} PRIVATE /arch:AVX2)
 endforeach()
endif()

add_executable(gradation_concurrent_tests tests/render/concurrent_raster_tests.cpp)
add_test(NAME gradation_concurrent_tests COMMAND gradation_concurrent_tests)

add_executable(gradation_precision_tests tests/render/precision_raster_tests.cpp)
add_test(NAME gradation_precision_tests COMMAND gradation_precision_tests)
if(MSVC)
 target_compile_options(gradation_precision_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_stability_tests tests/render/stability_tests.cpp)
add_test(NAME gradation_stability_tests COMMAND gradation_stability_tests)
if(MSVC)
 target_compile_options(gradation_stability_tests PRIVATE /arch:AVX2)
endif()
add_executable(gradation_precision_scalar_tests tests/render/precision_raster_tests.cpp)
add_test(NAME gradation_precision_scalar_tests COMMAND gradation_precision_scalar_tests)

add_executable(gradation_spectral_extended_tests tests/core/spectral_extended_tests.cpp)
add_test(NAME gradation_spectral_extended_tests COMMAND gradation_spectral_extended_tests)
set_tests_properties(gradation_spectral_extended_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")


add_executable(gradation_roi_tests tests/render/roi_tests.cpp tests/support/roi_scalar_reference.cpp)
set_source_files_properties(tests/render/roi_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
add_test(NAME gradation_roi_tests COMMAND gradation_roi_tests)

add_executable(gradation_blend_tests tests/render/blend_tests.cpp)
add_test(NAME gradation_blend_tests COMMAND gradation_blend_tests)
if(MSVC)
 target_compile_options(gradation_blend_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_stop_codec_tests tests/ui/stop_codec_tests.cpp)
add_test(NAME gradation_stop_codec_tests COMMAND gradation_stop_codec_tests)


add_executable(gradation_empty_source_tests tests/render/empty_source_tests.cpp)
add_test(NAME gradation_empty_source_tests COMMAND gradation_empty_source_tests)
if(MSVC)
 target_compile_options(gradation_empty_source_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_blend_avx2_tests tests/render/blend_avx2_tests.cpp tests/support/blend_scalar_reference.cpp)
set_source_files_properties(tests/render/blend_avx2_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
add_test(NAME gradation_blend_avx2_tests COMMAND gradation_blend_avx2_tests)

add_executable(gradation_blend_stability_tests tests/render/blend_stability_tests.cpp)
add_test(NAME gradation_blend_stability_tests COMMAND gradation_blend_stability_tests)
if(MSVC)
 target_compile_options(gradation_blend_stability_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_startup_host_tests tests/host/startup_host_tests.cpp)
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
 add_test(NAME gradation_effect_only_sdk_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/audit_effect_only.py")
endif()

add_test(NAME gradation_pipl_binary_contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/inspect_pipl_binary.py" "$<TARGET_FILE:Gradation>")

add_executable(gradation_blend_roi_avx2_tests tests/render/blend_roi_avx2_tests.cpp tests/support/blend_scalar_reference.cpp)
set_source_files_properties(tests/render/blend_roi_avx2_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
add_test(NAME gradation_blend_roi_avx2_tests COMMAND gradation_blend_roi_avx2_tests)



add_executable(gradation_lut_cache_tests tests/render/lut_cache_tests.cpp)
add_test(NAME gradation_lut_cache_tests COMMAND gradation_lut_cache_tests)
if(MSVC)
 target_compile_options(gradation_lut_cache_tests PRIVATE /arch:AVX2)
endif()

add_executable(gradation_stop_picker_tests tests/ui/stop_picker_tests.cpp)
target_link_libraries(gradation_stop_picker_tests PRIVATE user32 gdi32 comdlg32)
add_test(NAME gradation_stop_picker_tests COMMAND gradation_stop_picker_tests)

add_executable(gradation_spatial_reuse_tests tests/render/spatial_reuse_tests.cpp tests/support/roi_scalar_reference.cpp tests/support/blend_scalar_reference.cpp)
set_source_files_properties(tests/render/spatial_reuse_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
add_test(NAME gradation_spatial_reuse_tests COMMAND gradation_spatial_reuse_tests)


add_executable(gradation_angle_approx_tests tests/render/angle_approx_tests.cpp)
if(MSVC)
 target_compile_options(gradation_angle_approx_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_angle_approx_tests COMMAND gradation_angle_approx_tests)


add_executable(gradation_direct_srgb32_tests tests/render/direct_srgb32_tests.cpp)
if(MSVC)
 target_compile_options(gradation_direct_srgb32_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_direct_srgb32_tests COMMAND gradation_direct_srgb32_tests)


add_executable(gradation_direct_srgb16_tests tests/render/direct_srgb16_tests.cpp)
if(MSVC)
 target_compile_options(gradation_direct_srgb16_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_direct_srgb16_tests COMMAND gradation_direct_srgb16_tests)

add_executable(gradation_direct_srgb8_tests tests/render/direct_srgb8_tests.cpp)
if(MSVC)
 target_compile_options(gradation_direct_srgb8_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_direct_srgb8_tests COMMAND gradation_direct_srgb8_tests)

add_executable(gradation_flat_gradient_rows_tests tests/render/flat_gradient_rows_tests.cpp tests/support/blend_scalar_reference.cpp)
if(MSVC)
 set_source_files_properties(tests/render/flat_gradient_rows_tests.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
endif()
add_test(NAME gradation_flat_gradient_rows_tests COMMAND gradation_flat_gradient_rows_tests)


add_executable(gradation_multi_depth_mfr_stress_tests tests/render/multi_depth_mfr_stress_tests.cpp)
if(MSVC)
 target_compile_options(gradation_multi_depth_mfr_stress_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_multi_depth_mfr_stress_tests COMMAND gradation_multi_depth_mfr_stress_tests)


add_executable(gradation_stop_interpolation_tests tests/core/stop_interpolation_tests.cpp)
if(MSVC)
 target_compile_options(gradation_stop_interpolation_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_stop_interpolation_tests COMMAND gradation_stop_interpolation_tests)

add_executable(gradation_opaque_normal_equivalence_tests tests/render/opaque_normal_generator_equivalence_tests.cpp)
if(MSVC)
 target_compile_options(gradation_opaque_normal_equivalence_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_opaque_normal_equivalence_tests COMMAND gradation_opaque_normal_equivalence_tests)




add_executable(gradation_raster_parallel_tests tests/render/raster_parallel_tests.cpp)
if(MSVC)
 target_compile_options(gradation_raster_parallel_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_raster_parallel_tests COMMAND gradation_raster_parallel_tests)


add_executable(gradation_uniform_gradient_fold_tests tests/render/uniform_gradient_fold_tests.cpp)
if(MSVC)
 target_compile_options(gradation_uniform_gradient_fold_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_uniform_gradient_fold_tests COMMAND gradation_uniform_gradient_fold_tests)

add_executable(gradation_direct_srgb3_tests tests/render/direct_srgb3_tests.cpp)
if(MSVC)
 target_compile_options(gradation_direct_srgb3_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_direct_srgb3_tests COMMAND gradation_direct_srgb3_tests)



add_executable(gradation_lazy_f32_lut_tests tests/render/lazy_f32_lut_tests.cpp)
if(MSVC)
 target_compile_options(gradation_lazy_f32_lut_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_lazy_f32_lut_tests COMMAND gradation_lazy_f32_lut_tests)


add_executable(gradation_lazy_mfr_cache_tests tests/render/lazy_mfr_cache_tests.cpp)
if(MSVC)
 target_compile_options(gradation_lazy_mfr_cache_tests PRIVATE /arch:AVX2)
endif()
add_test(NAME gradation_lazy_mfr_cache_tests COMMAND gradation_lazy_mfr_cache_tests)





add_executable(gradation_transparent_alpha0_tests tests/render/transparent_alpha0_tests.cpp)
if(MSVC)
 target_compile_options(gradation_transparent_alpha0_tests PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_transparent_alpha0_tests COMMAND gradation_transparent_alpha0_tests)

add_executable(gradation_horizontal32_tests tests/render/horizontal32_tests.cpp)
if(MSVC)
 target_compile_options(gradation_horizontal32_tests PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_horizontal32_tests COMMAND gradation_horizontal32_tests)


add_executable(gradation_gray32_hash_base tests/render/gray32_abi_hash_tests.cpp)
add_executable(gradation_gray32_hash_fast tests/render/gray32_abi_hash_tests.cpp)
if(MSVC)
 target_compile_options(gradation_gray32_hash_base PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_gray32_hash_fast PRIVATE /arch:AVX2 /fp:precise)
endif()
target_compile_definitions(gradation_gray32_hash_base PRIVATE GRADATION_DISABLE_F32_GRAY_FAST)
add_test(NAME gradation_gray32_exact_parity
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_gray32_abi_hash.py"
 $<TARGET_FILE:gradation_gray32_hash_base>
 $<TARGET_FILE:gradation_gray32_hash_fast>)


add_executable(gradation_integer_row_cache_tests tests/render/integer_row_cache_tests.cpp)
if(MSVC)
 target_compile_options(gradation_integer_row_cache_tests PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_integer_row_cache_tests COMMAND gradation_integer_row_cache_tests)

add_executable(gradation_vertical_reuse_tests tests/render/vertical_reuse_tests.cpp)
add_executable(gradation_vertical_reuse_baseline tests/render/vertical_reuse_tests.cpp)
target_compile_definitions(gradation_vertical_reuse_baseline PRIVATE GRADATION_DISABLE_VERTICAL_REUSE)
if(MSVC)
 target_compile_options(gradation_vertical_reuse_tests PRIVATE /arch:AVX2 /fp:precise)
 target_compile_options(gradation_vertical_reuse_baseline PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_vertical_reuse_tests COMMAND gradation_vertical_reuse_tests)

add_executable(gradation_mapping16_io_tests tests/render/mapping16_io_tests.cpp)
add_executable(gradation_mapping16_io_baseline tests/render/mapping16_io_tests.cpp)
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
 -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/mapping16_io_compare.cmake")

add_executable(gradation_float_lut_io_tests tests/render/float_lut_io_tests.cpp)
add_executable(gradation_float_lut_io_baseline tests/render/float_lut_io_tests.cpp)
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
 -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/float_lut_io_compare.cmake")

add_executable(gradation_angle_row_tests tests/render/angle_row_tests.cpp)
add_executable(gradation_angle_row_baseline tests/render/angle_row_tests.cpp)
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
 -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/angle_row_compare.cmake")

add_executable(gradation_angle_cache_tests tests/render/angle_cache_tests.cpp)
if(MSVC)
 target_compile_options(gradation_angle_cache_tests PRIVATE /arch:AVX2 /fp:precise)
endif()
add_test(NAME gradation_angle_cache_tests COMMAND gradation_angle_cache_tests)

add_test(NAME gradation_angle_cache_cached_store_tests COMMAND gradation_angle_cache_tests)
set_tests_properties(gradation_angle_cache_cached_store_tests PROPERTIES ENVIRONMENT "GRADATION_ANGLE_CACHE_STREAM=0")

add_executable(gradation_geometry_cache_tests tests/render/geometry_cache_tests.cpp)
if(MSVC)
 target_compile_options(gradation_geometry_cache_tests PRIVATE /arch:AVX2 /fp:precise)
endif()

add_test(NAME gradation_geometry_cache_tests COMMAND gradation_geometry_cache_tests)

add_executable(gradation_ae_compute_cache_tests tests/host/ae_compute_cache_tests.cpp)
target_include_directories(gradation_ae_compute_cache_tests PRIVATE
 "${AE_SDK_ROOT}/Headers" "${AE_SDK_ROOT}/Headers/SP")
target_compile_definitions(gradation_ae_compute_cache_tests PRIVATE MSWindows WIN32 _WINDOWS)
target_compile_options(gradation_ae_compute_cache_tests PRIVATE /arch:AVX2 /fp:precise)
add_test(NAME gradation_ae_compute_cache_tests COMMAND gradation_ae_compute_cache_tests)



add_executable(gradation_blend_graph_tests tests/render/blend_graph_tests.cpp)
target_compile_options(gradation_blend_graph_tests PRIVATE /arch:AVX2 /fp:precise)
add_test(NAME gradation_blend_graph_tests COMMAND gradation_blend_graph_tests)

# Opt-in packed blend LUT A/B experiment.
