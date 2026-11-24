# Gradation architecture

## Dependency direction

- src/ae: Adobe SDK integration. Owns host lifetimes, parameter serialization,
  SmartFX requests, MFR-safe snapshots and native custom UI events.
- src/core: host-independent gradient/color interpolation and stop math.
- src/render/cpu: pixel kernels and bounded caches/scheduling. Consumes core
  values; does not depend on the Adobe SDK.
- src/render/gpu: platform backends. CUDA owns the canonical GPU pixel math;
  tools/generate_metal.py emits Metal from that math and a pointer-free ABI.
  GPU host adapters use the respective Adobe GPU device suites.
- src/ui/common: portable color model, codec, control geometry and picker
  interface. src/ui/windows and src/ui/macos own platform presentation.
- assets: original Figma exports and resource files. Generated C++/GPU
  artifacts belong under build, never in the source tree.

## Build boundaries

CMakeLists.txt dispatches to cmake/Windows.cmake or cmake/MacOS.cmake.
PortableTests.cmake is SDK-free and usable on Windows, Linux and macOS.
MetalLibrary.cmake builds the shader and optional standalone GPU smoke test.
WindowsTests.cmake retains the original test target names and CTest contracts.
Benchmarks.cmake defines opt-in targets; no benchmark is linked into Gradation.
The research and legacy sources in experiments are not product code.

## Contributor rules

- Keep the one canonical repository and shared Slugia dependency; do not
  create versioned sibling source directories or vendor Adobe SDK headers.
- Put new implementation in the owning module and tests in tests/core,
  tests/render, tests/ui or tests/host.
- Put new research code in experiments and performance drivers in benchmarks.
- Preserve the byte-identical Figma assets. Changes to exported source design
  must be deliberate and reflected in the asset hash audit.
- Never commit generated binaries, machine-local logs, SDKs, one-off patch
  scripts or snapshots; historical local materials stay under backups.
- Do not automatically install into AE, open/close the host or manipulate
  its GUI. Headless tests and compilation are the default validation.
- Keep public entry points, parameter IDs, PiPL and project compatibility
  stable. Validate both CPU and GPU paths when rendering behavior changes.

See README for build commands and MACOS_VALIDATION.md for physical Mac tests.

See [performance notes](PERFORMANCE.md) for measured kernel changes and reproducible benchmarks.
