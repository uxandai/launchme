# Impeller renderer (Linux)

On Linux every pixel and every glyph is drawn by [Impeller](https://github.com/flutter/flutter/tree/main/engine/src/flutter/impeller),
the standalone C SDK of Flutter's renderer, on the GPU (Vulkan, else OpenGL). There is no Skia and
no CPU renderer: a machine without a usable Vulkan or OpenGL driver shows blank windows. Windows
continues to use Direct2D and Direct3D.

## Layers

| Layer | Where | What |
|---|---|---|
| Kernel | `src/linux/impeller/` (`launchme_impeller`) | A small canvas API over Impeller: `Canvas` (save/layers/clips, rects, paths, images, paragraphs), `Path` (builders, flattening, hit testing, dashing, measuring, booleans through Clipper2), `Paint`/`Shader` (gradients with Direct2D's premultiplied interpolation, image shaders, blur and colour-matrix filters), `Image` (RGBA/BGRA, BC1/BC3 compressed uploads, readback, CPU mip-reduction), `TextParagraph` (typography), and the context/swapchain handling (`backend.cpp`). |
| Direct2D / DirectWrite / WIC ports | `src/linux/gfx/` | The COM-shaped surface the shared UI code calls, implemented on the kernel. Effects (blur, colour matrix, blend, affine, composite) use Impeller filters and blend modes; arithmetic composite and point-specular lighting are per-pixel maths on the CPU. DirectWrite layouts are Impeller paragraphs plus the DirectWrite placement rules (alignment, trimming, line spacing, UTF-16 <-> wchar_t). |
| Windows | `src/linux/window/gpu.cpp` | Picks Vulkan or OpenGL once and hands every window its canvas. |

Fonts come from fontconfig (system fonts, with fallback) and from files registered with the
typography context (the launcher's icon font, font sets). FreeType only reads font metrics.

Known gaps: DirectWrite character spacing is ignored (Impeller's typography has no letter
spacing); non-default line spacing moves the paragraph, not each line; compressed textures are
single-level (large minifications use CPU-reduced levels).

## Build

The SDK is pinned to Flutter engine revision `f6965468e5902c28fb719793f5da3e7e812870fb`
(standalone API 1.4.0) and carries Launchme's small C ABI extension (`launchme_impeller.h`:
compressed textures, render-to-texture, readback).

```sh
bash tools/linux/build-impeller.sh
# SDK: ~/launchme-impeller/sdk; override the workspace with IMPELLER_ROOT.

cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLAUNCHME_IMPELLER_DIR="$HOME/launchme-impeller/sdk"
cmake --build build/linux --parallel 8
ctest --test-dir build/linux --output-on-failure -R '^impeller_' -j 1
```

CMake checks the SDK revision, and context creation checks the upstream API version and the
extension ABI. `libimpeller.so` is placed beside `launchme` (and in the AppImage); its notice goes to
`licenses/impeller.txt`. Clipper2 (Boost licence) is fetched by CMake.

For the WSL workflow: `bash tools/linux/test-impeller.sh all` (mirror and build in `~/lm-impeller`).

## Selecting and diagnosing

```sh
LAUNCHME_RENDERER=vulkan ./launchme   # Vulkan, also a software device such as lavapipe
LAUNCHME_RENDERER=gl ./launchme       # OpenGL
LAUNCHME_VSYNC=1 ./launchme           # wait for the display's refresh
LAUNCHME_GPU_DEBUG=1 ./launchme       # frames per second and present time every 5 s
LAUNCHME_IMPELLER_VALIDATION=1 ./launchme   # Vulkan validation layers when installed
```

Without `LAUNCHME_RENDERER` the launcher tries a present-capable hardware Vulkan device, then
OpenGL. The chosen backend and the BC1/BC3 capability are printed to stderr.

## Verification

`impeller_backend_tests` exercises real GL/Vulkan uploads and pixels, BC1/BC3 alpha, invalid
inputs, premultiplied gradients, text, clips and layers, path booleans, GPU readback, resize and
teardown. `impeller_d2d_tests` covers the application's Direct2D entry points, compressed bitmap
creation, render-target copies and a target outliving its window. CTest uses private Xvfb displays
where available (llvmpipe/lavapipe, so these are functional, not performance, results).
`LAUNCHME_UI_CAPTURE_DIR` writes PNGs for inspection. An unavailable display or context is
reported as a skipped test.

`tools/linux/smoke-impeller.sh <build-dir> <capture-dir> <gl|vulkan> <desktop|tv>` runs the real
launcher with a fresh short-path profile on a private X server and captures screenshots.
