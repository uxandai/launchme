# Impeller renderer (Linux)

On Linux every pixel and every glyph is drawn by [Impeller](https://github.com/flutter/flutter/tree/main/engine/src/flutter/impeller),
the standalone C SDK of Flutter's renderer, on the GPU (Vulkan, else OpenGL). There is no Skia and
no CPU renderer: a machine without a usable Vulkan or OpenGL driver shows blank windows. Windows
continues to use Direct2D and Direct3D.

## Layers

| Layer | Where | What |
|---|---|---|
| Kernel | `src/linux/impeller/` (`launchme_impeller`) | A small canvas API over Impeller: `Canvas` (save/layers/clips, rects, paths, images, paragraphs), `Path` (builders, flattening, hit testing, dashing, measuring, booleans through Clipper2; a lone rectangle, rounded rectangle or oval is drawn and clipped as Impeller's own shape, other paths keep their converted Impeller path), `Paint`/`Shader` (gradients with Direct2D's premultiplied interpolation, image shaders, blur and colour-matrix filters), `Image` (RGBA/BGRA uploads into device-local textures, in-place replacement for video frames, readback, mip-reductions halved on the GPU), `TextParagraph` (typography), and the context/swapchain handling (`backend.cpp`). |
| Direct2D / DirectWrite / WIC ports | `src/linux/gfx/` | The COM-shaped surface the shared UI code calls, implemented on the kernel. Effects (blur, colour matrix, blend, affine, composite) use Impeller filters and blend modes; arithmetic composite, point-specular lighting and the straight-alpha colour matrix are the renderer's own fragment shaders (`src/linux/impeller/shaders`, compiled by `tools/linux/build-shaders.sh` into `effect_shaders.inc`), with the same maths on the CPU as a fallback. Frosted glass (`launchme_d2d.h`) blurs the scene recorded so far with Impeller's backdrop filter instead of copying the target. DirectWrite layouts are Impeller paragraphs plus the DirectWrite placement rules (alignment, trimming, line spacing, UTF-16 <-> wchar_t). |
| Windows | `src/linux/window/gpu.cpp` | Picks Vulkan or OpenGL once and hands every window its canvas. |

Fonts come from fontconfig (system fonts, with fallback) and from files registered with the
typography context (the launcher's icon font, font sets). FreeType only reads font metrics.

Known gaps: DirectWrite character spacing is ignored (Impeller's typography has no letter
spacing); non-default line spacing moves the paragraph, not each line; textures are single-level (large
minifications use GPU-halved levels); block-compressed artwork (BC1/BC3) is Windows-only.

## Build

The SDK is pinned to Flutter engine revision `f6965468e5902c28fb719793f5da3e7e812870fb`
(standalone API 1.4.0) and carries Launchme's small C ABI extension (`launchme_impeller.h`:
BGRA/RGBA textures and their in-place update, render-to-texture, readback).

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
LAUNCHME_VSYNC=0 ./launchme           # OpenGL: present at once (default waits for the refresh; adaptive: late frames tear)
LAUNCHME_GPU_DEBUG=1 ./launchme       # frames per second and present time every 5 s
LAUNCHME_IMPELLER_VALIDATION=1 ./launchme   # Vulkan validation layers when installed
```

Without `LAUNCHME_RENDERER` the launcher tries a present-capable hardware Vulkan device, then
OpenGL. The chosen backend is printed to stderr.

## Verification

`impeller_backend_tests` exercises real GL/Vulkan uploads and pixels, in-place texture updates, GPU
reductions, invalid inputs, premultiplied gradients, text, clips and layers, path booleans, GPU readback, resize and
teardown. `impeller_d2d_tests` covers the application's Direct2D entry points, refused block-compressed
formats, render-target copies and a target outliving its window. CTest uses private Xvfb displays
where available (llvmpipe/lavapipe, so these are functional, not performance, results).
`LAUNCHME_UI_CAPTURE_DIR` writes PNGs for inspection. An unavailable display or context is
reported as a skipped test.

`tools/linux/smoke-impeller.sh <build-dir> <capture-dir> <gl|vulkan> <desktop|tv>` runs the real
launcher with a fresh short-path profile on a private X server and captures screenshots.
