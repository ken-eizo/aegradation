# CPU performance notes

## 2026-11-24 — axis sampling reuse

Scope: FHD raster-core microbenchmarks on Intel Core i9-10900 / Windows.
No claim of real After Effects frame latency or Mac hardware performance.

### U16 vertical Linear / Replace RGB / Multiply Alpha

Exact packed Q15 color reused once per row instead of per eight pixels.
Same alpha multiplication, LUT quantization, output format and ROI behavior.
Only complete SIMD blocks with fully covered source qualify; other inputs use
the existing renderer. No additional workers or persistent image cache.

Median of three runs (each run measures 15 iterations):

| Workers | Previous path | New path |
|---:|---:|---:|
| 1 | 4.4314 ms | 2.0340 ms |
| 2 | 2.2768 ms | 1.6825 ms |

The baseline disables vertical reuse, which was already enabled for other
depths and generator paths. Only U16 Multiply is the new optimization
represented by these numbers.

### Portable non-AVX2 composite path

Bounded per-stripe 64 KiB scanline cache, or one sample per vertical row.
The exact scalar arithmetic order is preserved, and unusual coordinates
or incomplete source coverage fall back to the original path.

Windows non-AVX2, single thread, median of five interleaved iterations:

| Depth / direction / mode | Previous | New |
|---|---:|---:|
| 16 / horizontal / Normal | 150.858 ms | 118.358 ms |
| 16 / vertical / Normal | 148.283 ms | 117.086 ms |
| 32 / horizontal / Normal | 131.645 ms | 97.5173 ms |
| 32 / vertical / Normal | 134.388 ms | 96.1598 ms |

These timings are not representative of Apple Silicon hardware. Broader
NEON compositing is still needed. No all-mode sub-1-ms claim.

### Reproduce

Windows SDK build:

    cmake --build build --config Release --target gradation_vertical_reuse_tests gradation_vertical_reuse_baseline gradation_scalar_axis_cache_tests --parallel 1
    ctest --test-dir build -C Release -R "gradation_(scalar_axis_cache_tests|vertical_reuse_tests)$" --output-on-failure

PowerShell benchmark (no AE launch):

    $env:GRADATION_CPU_WORKERS = "2"
    & .\build\Release\gradation_vertical_reuse_baseline.exe
    & .\build\Release\gradation_vertical_reuse_tests.exe
    $env:GRADATION_AXIS_BENCH = "1"
    & .\build\Release\gradation_scalar_axis_cache_tests.exe
    Remove-Item Env:GRADATION_AXIS_BENCH

Portable build runs the same scalar parity test by default without the
benchmark environment variable. 6,336 combinations per depth, including
odd widths, negative row strides, all 33 color modes, six representative
alpha modes, stochastic blending, HDR/nonfinite source channels, and
direct/LUT sampling. Output and row padding are compared byte for byte.
