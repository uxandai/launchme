# Renderer decision: Skia and Impeller

Implementation update: an opt-in hybrid backend now exists on `feat/implerrer-backend`, including
BC1/BC3, GL/Vulkan integration and the restart-bound on/off setting. Build instructions,
verification results and current limitations are in [IMPELLER.md](IMPELLER.md). The assessment
below records the investigation that preceded that implementation.

**Reviewed: 2026-10-31. Decision: retain Skia for the Linux backend.** Impeller is a viable
native rendering library, but replacing the current backend is not justified by the available
evidence. The integration gaps below would require substantial additional work, and no
Launchme Skia-versus-Impeller benchmark has demonstrated a benefit.

**BC1 follow-up, same date:** the Impeller core already supports BC1/BC3. The standalone C API
does not expose them. Extending that adapter is a credible way to retain compressed artwork;
BC1 should be treated as scoped integration work, not a fundamental engine blocker. A focused
compressed-texture prototype is justified before deciding on a full renderer migration.

## Scope and evidence

Launchme is a native C++ application. Its shared UI calls Direct2D/DirectWrite interfaces:

- Windows uses Direct2D/DirectWrite. The current working tree also has a Direct3D 11 flip-model
  swap chain and video textures on the renderer's device (`src/ui/swap_chain.cpp`,
  `src/platform/video_player_windows.cpp`). Replacing Linux Skia would not accelerate this path.
- Linux implements those interfaces in `src/linux/gfx`. Drawing uses Skia Ganesh on Vulkan,
  then OpenGL, with a CPU raster fallback; SDL3 handles windows and presentation
  (`src/linux/window/gpu.cpp`). Text uses SkParagraph, HarfBuzz, ICU and Fontconfig.
- `cmake/LinuxApp.cmake` links Skia, SkParagraph, SkShaper, SkUnicode, SkPathOps and SkCMS.
  `tools/linux/build-skia.sh` builds this selected subset, with image codecs and PDF/SVG disabled.

This assessment covers the inspected working tree, including its compressed-texture implementation.
It is a source/API compatibility review, not a measurement of relative rendering speed.

The upstream review uses Flutter revision
[`f6965468e5902c28fb719793f5da3e7e812870fb`][upstream], whose standalone header reports API
**1.4.0**. The [standalone SDK][sdk] has a C API, a C++ wrapper and Linux/Windows artifacts.
Flutter's [platform availability][availability] also lists desktop Impeller support. Platform
availability is therefore not the reason to defer this migration: Launchme needs its own
adapter rather than Flutter's renderer-selection setting.

## What Impeller offers

Impeller's precompiled shaders and explicit pipeline management are attractive for reducing
shader-compilation stalls. Its reusable display lists, Vulkan/OpenGL integration, text layout,
gradients, clipping, blending, blur and custom fragment programs make a native port feasible.
Its Vulkan swapchain API could replace some of Launchme's own presentation machinery.

Those capabilities do not establish lower frame time, memory use or power consumption for
Launchme. The current Linux backend already uses the GPU and persists compiled shaders and
pipelines in `$XDG_CACHE_HOME/launchme/skia` (or `~/.cache/launchme/skia`). This helps warm
runs; it does not eliminate all first-use compilation stalls. Cold and warm runs both matter.

## Compatibility work required

These observations concern the inspected **public standalone API**, not everything that could
be implemented by modifying Impeller's internals or adding another library.

| Requirement in Launchme | Current implementation | Consequence for a migration |
| --- | --- | --- |
| Compressed artwork on the GPU | `d2d_bitmap.cpp::createCompressed` uploads BC1 directly when the device supports it. | The standalone `ImpellerPixelFormat` exposes RGBA8888 only, but the core already implements BC1/BC3 and block-aware uploads. Extend the API adapter or use the internal C++ API to preserve compression (see the follow-up below). An RGBA fallback would require eight times an aligned BC1 texture's base-level storage, but that expansion is avoidable on supported devices; it is not an estimate of total process memory. |
| CPU targets and captures | `d2d_wic.cpp` draws into a raster surface and copies pixels into a WIC bitmap; `gpu.cpp` also has a CPU window fallback. | The SDK requires a GPU and explicitly has no dedicated software renderer. Software Vulkan/GL drivers are possible, but are a different dependency and execution path. Retaining Skia for CPU work would make this a hybrid backend, rather than removing Skia. |
| Offscreen images, copies and readback | `d2d_bitmap.cpp` uses renderable surfaces, snapshots, partial updates and render-target copies. | The public API exposes GL FBO wrapping, Metal drawable wrapping and Vulkan swapchain surfaces, but no general Vulkan offscreen render-to-texture/readback interface equivalent to the current contract. Backend-specific integration or an API extension is required; drawing to a window alone is insufficient. |
| Path measurements and boolean geometry | `d2d_geometry.cpp` uses SkContourMeasure and SkPathOps. Card highlights use length/tangent queries; controller shapes and companion bubbles use intersection/union. | The API builds and draws paths and returns conservative bounds, but does not expose equivalent path-length, point-at-length, containment or boolean-operation calls. Keep a geometry library or implement and validate replacements. Clipping alone does not produce the resulting geometry used by these callers. |
| Effect graph | `d2d_effects.cpp` implements blur, matrices, saturation, blending, affine transforms, arithmetic composition and point-specular lighting. Coverflow uses the latter two effects. | Several effects have direct building blocks. Arithmetic composition, point-specular lighting and exact alpha/bounds semantics need adapter work or custom shaders and visual regression checks. The SDK does support custom fragment programs; these effects are not inherently impossible. |
| DirectWrite-compatible text | `dwrite_layout.cpp` and `dwrite_render.cpp` adapt wrapping, trimming, line metrics, range effects, character spacing and UTF-32/UTF-16 offsets. | SDK paragraph, line-metric and glyph APIs provide a starting point, not a drop-in DirectWrite implementation. Validate the existing layout contract. The SDK's own typography implementation still uses Skia text/font components, so adopting its text API does not itself remove every Skia dependency. |
| Dependency lifecycle and distribution | CMake builds the current adapter against a local Skia build; AppImage packaging ships the resulting application. | The standalone SDK explicitly gives no API/ABI stability guarantee. Pin the exact headers/library revision and archive digest, check runtime version compatibility, and validate build dependencies, licenses and the packaged application. |

The OpenGL texture-handle API is a useful interoperability escape hatch. It does not establish
equivalent support for BC1 uploads, texture updates and offscreen images on the Vulkan path.
Likewise, a custom C++ integration can expose more of Impeller, but increases the amount of
engine-specific code and update maintenance owned by Launchme.

## BC1 follow-up: an existing upstream solution

The first review stopped at the public standalone API. Inspection of the core at the **same
revision** establishes that the GPU implementation is already present:

- [PR #187077][compressed-pr], merged **2026-05-28**, added BC1/BC3/BC5/BC7, ETC2 and ASTC
  formats, block-aware byte counts, backend mappings/uploads and capability queries.
- [PR #189303][compression-vulkan-pr], merged **2026-08-04**, enabled the Vulkan compression
  features at device creation. The inspected `capabilities_vk.cc` contains
  `required.textureCompressionBC = supported.textureCompressionBC`, and the capability query
  reads the enabled feature. An older build missing this fix is not an adequate prototype base.
- [`core/formats.h`][core-formats] contains `PixelFormat::kBC1RGBAUNormInt` and
  `kBC3RGBAUNormInt`, with 4x4 blocks of 8 and 16 bytes respectively. Vulkan maps them to
  `eBc1RgbaUnormBlock` and `eBc3UnormBlock`; the GLES upload path uses `CompressedTexImage2D`.
- Upstream has a `CanRenderBC1CompressedTexture` rendering golden test. That is evidence of an
  implemented/testable path, not a claim that the test has been run on this machine.
- [`interop/formats.h`][interop-formats] maps only `kImpellerPixelFormatRGBA8888` to the core.
  [`interop/impeller.cc`][interop-api] already forwards texture creation and content upload to
  the core allocator and texture. This is the missing API bridge, not a missing BC1 codec.

The umbrella [issue #148443][compression-issue] remains open for the broader feature. Its open
status does not mean the core implementation is absent; conversely, duplicate issue #187072
being closed was not itself proof of implementation. The merged PRs and source provide that proof.

### Recommended prototype: extend the standalone adapter

1. Build a pinned SDK from source containing both merged changes. Add explicit BC1/BC3 enum
   values and mappings in `impeller.h` and `interop/formats.h`, plus a capability query backed
   by `Capabilities::SupportsTextureCompression(CompressedTextureFamily::kBC)`. Version the
   extension and keep its headers, exported functions and C++ bindings with the matching library.
   Editing just the application header or passing an internal enum value to the stock binary
   would not work: its conversion function falls back to RGBA8888.
2. Upload Launchme's existing `block_compression`/`TextureCache` blocks directly, with validated
   dimensions, block byte counts and upload lifetime. Keep textures sample-only, as required
   by the allocator. Start with one mip level, matching the current Linux BC1 path; the inspected
   standalone creation function clamps the mip count to at most one, so it must not be mistaken
   for an API that uploads a complete precomputed mip chain.
3. Sample those textures through the normal display-list image path. Retain uncompressed
   render targets for effects and video; compressed source artwork can still be sampled into
   them. Preserve the current UNORM/color interpretation and test BC3 premultiplied alpha.
4. Verify Vulkan and OpenGL draws, unsupported-device fallback, malformed input rejection and
   repeated create/draw/release. A GPU capture must show an actual BC1/BC3 texture, with correct
   pixels, rather than just a compressed file expanded before upload. Measure upload and GPU
   memory costs before declaring the adapter complete.

This is narrower than implementing compression in a rendering engine: the upstream backend
work can be reused. It still requires a source-built SDK, adapter tests and maintenance of the
patch. No such patch or runtime prototype was built during this research follow-up.

### Other options

| Option | Assessment for Launchme |
| --- | --- |
| Internal Impeller C++ API | Exposes the compressed formats and allocator directly. Viable, but ties more application code to engine internals and its build system than a small standalone-API extension. |
| Upload with OpenGL and wrap a texture handle | The standalone API supports wrapping GL textures, but its descriptor still cannot describe BC1. Presenting a BC1 texture as RGBA would be an unverified metadata mismatch. This also does not solve the Vulkan path; prefer the explicit format extension. |
| KTX2 / Basis Universal | An established portable container/transcoding approach, also suggested in the upstream issue. It can select BC/ETC2/ASTC for different GPUs, but cannot bypass a missing standalone upload API. Launchme already produces and caches BC1/BC3 blocks, so this extra asset pipeline is unnecessary for the desktop prototype. See [Basis Universal][basis]. |
| Expand to RGBA with a smaller resident cache | A functional fallback when compression is unavailable. It trades memory or image resolution/cache retention against upload work; it is not needed as the primary path on a BC-capable device once the adapter exposes support. |

## Decision

Keep the current Linux renderer as the production default while evaluating a focused prototype.
**BC1 alone is no longer a reason to reject Impeller:** the core supports it and a bounded API
extension can expose it. The other integration requirements in the table, and the need for an
application-specific performance result, still apply to a complete migration.

The existing Windows performance results cannot establish whether Linux Skia is a bottleneck.
A successful current build or regression run also establishes only the current baseline, not
Impeller compatibility or performance. No Impeller runtime or A/B rendering benchmark was run
for this assessment.

### Baseline verification

On the review date, the current Windows working tree built successfully in Release
(`cmake --build build/temp/vs --config Release --parallel 4`). The five targeted CTest cases
for cached text pixels, block compression, image decoding, low-memory mode and desktop UI
passed, followed by **128/128** passing cases in the full suite
(`ctest --test-dir build/temp/vs -C Release --output-on-failure -j 1`). The build reported an
MSB8028 intermediate-directory warning for the sentencepiece dependency. Linux execution and
Impeller runtime behavior were not tested in this review.

## Stages for reopening the decision

1. **Identify a measured Linux bottleneck.** Use controlled offline artwork and repeatable TV,
   desktop-scroll, text, blur and coverflow scenes on Steam Deck and a desktop GPU. Record the
   actual backend/device, resolution, refresh rate and driver; measure cold/warm p95/p99 frame
   times, missed-frame counts, CPU/GPU work, startup, upload cost, RAM and GPU memory. Do not
   interpret time spent waiting for presentation as pure GPU execution time.
2. **Prove the difficult contracts in an isolated prototype.** Pin the SDK and exercise BC1,
   offscreen snapshots/copies/readback, CPU-target policy, geometry operations, text metrics and
   effect graphs. State explicitly which Skia components remain. Exit only when each required
   contract has a working, tested owner and its copy/memory cost is known.
3. **Integrate an optional Linux backend.** Reuse the existing Direct2D/DirectWrite-facing
   boundary; implement SDL lifecycle, resize, presentation, resource ownership, context failure
   and fallback. Do not advertise a backend selector before it draws the real application.
4. **Establish visual and behavioral parity.** Compare captured PNGs for TV layouts, desktop,
   coverflow, clips, alpha, multilingual text, DPI and video. Build and run all tests on both
   platforms. The legacy `src/linux/gfx/tests/{d2d,dwrite}_smoke.cpp` files still contain Cairo-era
   stubs and are not registered in the Linux CMake build; their existence is not renderer test
   coverage. Repair/register them or provide equivalent executable coverage before relying on them.
5. **Decide from repeated A/B results and a packaged run.** Use the same hardware, scenes and
   workload as stage 1. Require a reproducible improvement in the identified bottleneck with
   acceptable startup, memory and power costs, passing visual checks and a clean AppImage run.
   Make Impeller the default only after this result; otherwise retain Skia.

[upstream]: https://github.com/flutter/flutter/tree/f6965468e5902c28fb719793f5da3e7e812870fb/engine/src/flutter/impeller/toolkit/interop
[sdk]: https://github.com/flutter/flutter/blob/f6965468e5902c28fb719793f5da3e7e812870fb/engine/src/flutter/impeller/toolkit/interop/README.md
[availability]: https://docs.flutter.dev/perf/impeller

API details: [impeller.h][api]. Typography implementation: [typography_context.cc][typography].

[api]: https://github.com/flutter/flutter/blob/f6965468e5902c28fb719793f5da3e7e812870fb/engine/src/flutter/impeller/toolkit/interop/impeller.h
[typography]: https://github.com/flutter/flutter/blob/f6965468e5902c28fb719793f5da3e7e812870fb/engine/src/flutter/impeller/toolkit/interop/typography_context.cc
[compressed-pr]: https://github.com/flutter/flutter/pull/187077
[compression-vulkan-pr]: https://github.com/flutter/flutter/pull/189303
[compression-issue]: https://github.com/flutter/flutter/issues/148443
[core-formats]: https://github.com/flutter/flutter/blob/f6965468e5902c28fb719793f5da3e7e812870fb/engine/src/flutter/impeller/core/formats.h
[interop-formats]: https://github.com/flutter/flutter/blob/f6965468e5902c28fb719793f5da3e7e812870fb/engine/src/flutter/impeller/toolkit/interop/formats.h
[interop-api]: https://github.com/flutter/flutter/blob/f6965468e5902c28fb719793f5da3e7e812870fb/engine/src/flutter/impeller/toolkit/interop/impeller.cc
[basis]: https://github.com/BinomialLLC/basis_universal
