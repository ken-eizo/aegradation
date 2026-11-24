set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(AE_SDK_ROOT "" CACHE PATH "Adobe After Effects SDK Examples directory")
include("${CMAKE_CURRENT_LIST_DIR}/PortableTests.cmake")
foreach(f Headers/AEConfig.h Headers/AE_EffectGPUSuites.h Util/entry.h Resources/AE_General.r)
 if(NOT EXISTS "${AE_SDK_ROOT}/${f}")
  message(FATAL_ERROR "Set AE_SDK_ROOT to Adobe SDK Examples; missing ${f}")
 endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/MetalLibrary.cmake")
add_library(Gradation MODULE src/ae/Gradation.cpp src/render/gpu/metal/gradation_metal_host.mm src/ui/macos/picker_macos.mm)
add_dependencies(Gradation gradation_metal_sources gradation_metal_library)
target_include_directories(Gradation PRIVATE "${AE_SDK_ROOT}/Headers" "${AE_SDK_ROOT}/Headers/SP"
 "${AE_SDK_ROOT}/Util" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(Gradation PRIVATE PF_DEEP_COLOR_AWARE=1)
target_compile_features(Gradation PRIVATE cxx_std_20)
target_compile_options(Gradation PRIVATE -ffp-contract=off -fvisibility=hidden "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
option(GRADATION_MAC_INTEL_AVX2 "Enable AVX2 on the x86_64 slice only (requires AVX2-capable Intel)" OFF)
if(GRADATION_MAC_INTEL_AVX2)
 target_compile_options(Gradation PRIVATE -Xarch_x86_64 -mavx2)
endif()
target_link_libraries(Gradation PRIVATE Threads::Threads "-framework Metal" "-framework Foundation" "-framework AppKit" "-framework CoreGraphics")
set_target_properties(Gradation PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION plugin PREFIX ""
 MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
 MACOSX_BUNDLE_GUI_IDENTIFIER "com.aegradation.effect"
 MACOSX_BUNDLE_BUNDLE_NAME "Gradation"
 MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
 MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
 LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/$<CONFIG>")
set(PIPL "${CMAKE_CURRENT_BINARY_DIR}/Gradation.rsrc")
add_custom_command(OUTPUT "${PIPL}"
 COMMAND "${XCRUN}" -sdk macosx Rez -d Rez=1 -d __aarch64__=1 -I "${AE_SDK_ROOT}/Headers" -I "${AE_SDK_ROOT}/Resources" "${CMAKE_CURRENT_SOURCE_DIR}/assets/ae/GradationPiPL.r" -o "${PIPL}"
 DEPENDS assets/ae/GradationPiPL.r "${AE_SDK_ROOT}/Resources/AE_General.r" "${AE_SDK_ROOT}/Headers/AEConfig.h" "${AE_SDK_ROOT}/Headers/AE_EffectVers.h" VERBATIM)
add_custom_target(gradation_macos_pipl DEPENDS "${PIPL}")
add_dependencies(Gradation gradation_macos_pipl)
add_custom_command(TARGET Gradation POST_BUILD
 COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:Gradation>/Contents/Resources"
 COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PIPL}" "$<TARGET_BUNDLE_DIR:Gradation>/Contents/Resources/Gradation.rsrc"
 COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:Gradation>" VERBATIM)
