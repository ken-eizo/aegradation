# Reused by the SDK plugin and the standalone Metal hardware smoke test.
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
find_program(XCRUN xcrun REQUIRED)
set(METAL_AIR "${CMAKE_CURRENT_BINARY_DIR}/gradation.air")
set(METAL_LIB "${CMAKE_CURRENT_BINARY_DIR}/gradation.metallib")
set(METAL_BLOB "${CMAKE_CURRENT_BINARY_DIR}/gradation_metal_blob.hpp")
add_custom_command(OUTPUT "${METAL_BLOB}" BYPRODUCTS "${METAL_AIR}" "${METAL_LIB}"
 COMMAND "${XCRUN}" -sdk macosx metal -std=macos-metal2.4 -fno-fast-math -ffp-contract=off -c "${METAL_GENERATED}" -o "${METAL_AIR}"
 COMMAND "${XCRUN}" -sdk macosx metallib "${METAL_AIR}" -o "${METAL_LIB}"
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_metal.py" "${METAL_LIB}" "${METAL_BLOB}"
 DEPENDS "${METAL_GENERATED}" tools/embed_metal.py VERBATIM)
add_custom_target(gradation_metal_library DEPENDS "${METAL_BLOB}")
add_dependencies(gradation_metal_library gradation_metal_sources)
add_executable(gradation_metal_smoke tests/host/metal_smoke.mm)
add_dependencies(gradation_metal_smoke gradation_metal_library)
target_include_directories(gradation_metal_smoke PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(gradation_metal_smoke PRIVATE -fobjc-arc -ffp-contract=off)
target_link_libraries(gradation_metal_smoke PRIVATE "-framework Metal" "-framework Foundation" Threads::Threads)
add_test(NAME gradation_metal_smoke COMMAND gradation_metal_smoke)
set_tests_properties(gradation_metal_smoke PROPERTIES SKIP_RETURN_CODE 77)
