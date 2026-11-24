# Gradation

Native Adobe After Effects effect plugin. Linear / Radial / Angle / Mapping, up to 32 gradient stops, multiple color spaces and separate color/alpha blending. Shared SmartFX/MFR render core; Windows CUDA and an initial native macOS Metal port.

## Repository layout

See [architecture and contributor rules](docs/ARCHITECTURE.md).

| Directory | Responsibility |
|---|---|
| `src/ae/` | Adobe SDK entry point, SmartFX, UI events, host cache |
| `src/core/` | Gradient math, color spaces and stop interpolation |
| `src/render/cpu/` | CPU kernels, AVX2, NEON and caching |
| `src/render/gpu/` | CUDA and Metal backends; shared GPU parameter ABI |
| `src/ui/` | Shared UI model, Windows/Slugia and macOS/AppKit |
| `assets/` | Original Figma designs, PiPL resource, native cursors |
| `tests/` | Core/render/UI/host tests, fixtures and test utilities |
| `benchmarks/` | Explicit opt-in performance benchmarks |
| `experiments/` | Research and legacy prototypes; not linked to the plugin |
| `cmake/` | Platform build and test definitions |
| `tools/`, `scripts/` | Build generators and collaborator entry points |
| `docs/` | Architecture and validation documentation |

Build products live in ignored `build/` and `dist/`. Historical local scripts and logs stay in ignored `backups/`, not in the public repository. Build a benchmark explicitly with `cmake --build build --config Release --target gradation_benchmark_cpu_matrix`.

## Status (2026-11-24)

- Windows x64: existing AVX2 CPU (8/16/32 bpc) and CUDA SmartFX GPU (32 bpc). Release AEX and focused SDK tests rebuilt after the platform split.
- macOS: build-ready port **pending first Xcode/AE hardware verification**. CPU 8/16/32 bpc shared scalar implementation, ARM64 NEON horizontal 32 bpc specialization, bounded Apple GCD task scheduling. Intel slice uses portable CPU by default; AVX2 is opt-in.
- macOS Metal: offline-generated MSL shares the CUDA pixel math. Host GPU worlds and the AE command queue are used directly; no CUDA/Vulkan or CPU readback. AE's GPU SDK path is 32 bpc. CPU fallback remains available.
- macOS Color picker: CPU-drawn native AppKit modal, HSV/RGB/alpha axes and exact depth-aware numeric entry. Commit on OK, cancel does not write AE parameters. Not a pixel-identical recreation of the Windows Figma-based Slugia modal; in-host live preview and final visual validation are pending.
- Verified on Windows: 19 portable tests including non-AVX2 CPU and Metal host parameter ABI; four focused Windows SDK tests. Actual ARM64, Metal compilation, Mac host behavior, performance and production signing remain to be tested.

## macOS — first build

Requirements: macOS 13+ build target (use a macOS/AE version supported by Adobe), Xcode with Metal compiler and SDK, CMake >= 3.24, Python 3, Git, and the official [After Effects C++ SDK](https://developer.adobe.com/after-effects/). Use the SDK's **Examples** directory containing `Headers`, `Util` and `Resources`. Do not redistribute SDK headers, sample sources or tools. If Xcode cannot find Metal, install the Metal toolchain through Xcode settings or `xcodebuild -downloadComponent MetalToolchain` where supported.

```sh
git clone https://github.com/ken-eizo/aegradation.git
cd aegradation
export AE_SDK_ROOT="/absolute/path/to/AfterEffectsSDK/Examples"
bash scripts/build_macos.sh
```

Default build targets the native architecture. Set `GRADATION_ARCHS="arm64;x86_64"` for a Universal binary (Intel host availability depends on the AE version). Output: `dist/Release/Gradation.plugin`. The script checks the exported EffectMain, PluginDataEntryFunction2, PiPL resource, architecture and ad-hoc signature. No automatic install, host launch, quarantine bypass or system setting change.

For a development test, manually copy the complete bundle to the appropriate AE Plug-ins directory after closing AE, then launch AE yourself. Ad-hoc signing is for local testing, not distribution/notarization.

## SDK-free CPU and Metal tests

```sh
cmake -S . -B build/core -DGRADATION_CORE_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel 2
ctest --test-dir build/core --output-on-failure

# macOS only: offline Metal compile plus optional hardware test
cmake -S . -B build/metal -DGRADATION_CORE_ONLY=ON -DGRADATION_TEST_METAL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal --parallel 2
ctest --test-dir build/metal --output-on-failure
```

Metal smoke test skips if no Metal device is available; on a physical Mac this is a required test. It compares 48 shape/blend/direct-vs-LUT cases against CPU, including odd image dimensions. SDK-free tests do not certify the Adobe host ABI.

## Windows build

Use Visual Studio 2022, CUDA toolkit with nvcc, Vulkan SDK, Python 3, CMake and the Adobe AE SDK. Slugia is a separate checkout, not embedded in Gradation. Current validated dependency is commit `91ba0b0562236fcff3fddbdb97f63dc7ea75621a` plus `dependencies/slugia-gradation.patch` (local changes required by the Windows picker). On a fresh checkout only:

```powershell
git clone --recursive https://github.com/ken-eizo/slugia.git ..\slugia
git -C ..\slugia checkout 91ba0b0562236fcff3fddbdb97f63dc7ea75621a
git -C ..\slugia submodule update --init --recursive
git -C ..\slugia apply --check "$PWD\dependencies\slugia-gradation.patch"
git -C ..\slugia apply "$PWD\dependencies\slugia-gradation.patch"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DAE_SDK_ROOT="C:/path/to/SDK/Examples" -DGRADATION_SLUGIA_SOURCE="../slugia"
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

Do not apply the patch twice or overwrite a modified Slugia checkout. Windows output is `dist/Release/Gradation.aex`. No automatic deployment.

## Mac collaborator verification

See [macOS validation](docs/MACOS_VALIDATION.md). Report build logs, architecture, SDK/AE/macOS versions, CPU/GPU test results and crash reports. Do not describe the port as fully supported until physical Mac checks pass.
