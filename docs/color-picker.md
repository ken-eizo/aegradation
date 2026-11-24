# Color picker

## Source ownership

- assets/figma/ColorPicker.slugui: canonical 797×444 Figma export.
- assets/figma/Button-State.slugui: authored component visual states.
- assets/figma/Gradient_CustomUIGenerated.slugui: original gradient design.
- assets/windows: cursor resources. Rebuild with tools/generate_native_cursors.py.
- The original bytes are preserved; tests/tools/audit_effect_only.py verifies
  the original picker hash and the SDK/OS modal contracts.

## Windows

src/ui/windows owns the Figma AOT scene, input routing, the native modal
lifecycle and the temporary CPU first paint. The exact original geometry is
compiled via Slugia; GPU initialization is scoped to the dialog. The picker
sizes its client area based on DPI and preserves the alpha-inclusive aspect
ratio. The model and numerical edit codec are shared with macOS.

A color change triggers a bounded UI-thread preview callback. The AE adapter
updates the arbitrary stop parameter and requests invalidation/refresh.
Cancel restores the original parameter; OK commits the final color. Actual
live composition rendering depends on AE host behavior and requires manual
verification. No host hooks, background UI workers or independent UI process.

## macOS

src/ui/macos implements a native AppKit CPU-drawn modal using the shared
color axes and depth-aware numeric codec. It does not depend on Slugia,
Vulkan or CUDA. Visual parity and in-host live preview are not yet complete.

## Gradient custom control

src/ui/common/gradient_custom_ui_layout.hpp defines the responsive geometry.
The AE event adapter draws using the host's current_frame and the same
coordinates for hit testing. Horizontal resizing preserves stop sizes and
vertical geometry. Layout verification is SDK-free; actual host panel
resizing must be confirmed manually.

## Verification

Use the CTest targets gradation_custom_ui_layout_tests,
gradation_picker_axes_tests and, on Windows,
gradation_slugui_source_fidelity_tests, gradation_picker_native_keyboard_headless,
gradation_picker_null_owner_headless and gradation_effect_only_sdk_audit.
No test in the default CTest suite launches After Effects or opens a visible
picker. Do not invoke the modal lifetime diagnostic without --headless.
