# Optional Impeller backend (Linux)

The `feat/implerrer-backend` implementation adds a working, opt-in Impeller renderer alongside
Skia. In a build that includes the SDK, **Settings → System → Performance → Impeller
(experimental)** selects the renderer for the next process. It is **off by default** and needs
a restart. The preference is saved as `impeller=0|1` in `settings.ini`.

## Build

Use the normal Linux dependencies and Skia build described in [linux-port.md](linux-port.md).
The additional SDK build requires Git, Python 3, curl, unzip and Ninja. It downloads the engine
dependencies and toolchain into its own directory; it does not require a Dart/Flutter application.

```sh
bash tools/linux/build-impeller.sh
# SDK: ~/launchme-impeller/sdk; override the workspace with IMPELLER_ROOT.

cmake -S . -B build/linux-impeller -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLAUNCHME_WITH_IMPELLER=ON \
  -DLAUNCHME_IMPELLER_DIR="$HOME/launchme-impeller/sdk"
cmake --build build/linux-impeller --parallel 8
ctest --test-dir build/linux-impeller --output-on-failure -R '^impeller_' -j 1
```

`LAUNCHME_WITH_IMPELLER` defaults to `OFF` for builders without the additional SDK. Such builds
use the small stub, have no Impeller library dependency and hide the unavailable setting.
Both implementations are included when the build option is `ON`; one GPU renderer is selected
at startup. Windows continues to use its Direct2D/Direct3D renderer.

The engine is pinned to `f6965468e5902c28fb719793f5da3e7e812870fb` (standalone API 1.4.0).
The SDK recipe targets Linux x64, matching the current AppImage.
`tools/linux/prepare-impeller.py` installs the separate `LaunchmeImpeller*` C ABI extension
from `tools/linux/impeller/` into that checkout. CMake checks the revision, and context creation
checks the upstream API version and extension ABI. Use the matching library and headers.

For this repository's WSL workflow, an isolated application mirror/build is available:

```sh
bash tools/linux/test-impeller.sh all
# Mirror and build: ~/lm-impeller/src and ~/lm-impeller/build.
# LM_IMPELLER_TEST_ROOT, LM_JOBS and IMPELLER_ROOT override the defaults.
```

## Selecting and diagnosing a renderer

```sh
LAUNCHME_IMPELLER=1 ./build/linux-impeller/launchme  # override the preference
LAUNCHME_IMPELLER=0 ./build/linux-impeller/launchme  # force Skia, including recovery
LAUNCHME_IMPELLER=1 LAUNCHME_RENDERER=gl ./build/linux-impeller/launchme
LAUNCHME_IMPELLER=1 LAUNCHME_RENDERER=vulkan ./build/linux-impeller/launchme
```

The existing `LAUNCHME_RENDERER` selects Vulkan/OpenGL/CPU independently of the saved engine
preference. Explicit `vulkan` also permits a software Vulkan device for testing. `cpu` uses
Skia's raster path. Automatic Impeller selection tries a present-capable hardware Vulkan
device, then OpenGL. If the Impeller OpenGL context cannot initialize, the window layer tries
Skia OpenGL. The selected engine and BC1/BC3 capability are printed to stderr.

`LAUNCHME_IMPELLER_VALIDATION=1` requests Vulkan validation when the host provides the layers.
The selected backend is process-wide; toggling a row does not replace a live device or window.

## Implementation and current limits

- `src/linux/impeller/` records shapes, paths, gradients, images, clips and opacity layers as
  native Impeller display lists. SDL owns windows; Impeller owns its Vulkan swapchains or draws
  to the current SDL OpenGL framebuffer.
- The existing Direct2D-facing UI remains shared. Skia supplies CPU geometry/boolean operations,
  SkParagraph layout, raster text cached as GPU images, WIC and compatible bitmap targets.
  Effect graphs still run through the existing Skia implementation on the CPU and upload their
  results. This is a hybrid implementation; moving expensive effects to native Impeller filters
  is subsequent optimization work.
- BC1/BC3 source images retain their blocks and upload them directly into compressed textures.
  A lazy CPU decoder serves effects and CPU captures when they need pixels. The GL capability
  query checks the required S3TC formats rather than requiring the entire BC1-through-BC7 family.
  Compressed textures are sample-only. The current image path uses one mip level and nearest
  or bilinear sampling; it does not reproduce Skia's mipmapped high-quality minification yet.
- Gradients preserve Direct2D's premultiplied interpolation by adaptively subdividing stops for
  the standalone API's straight-color interpolation. Tests bound the remaining GL/Vulkan versus
  CPU differences; exact pixels across rendering engines are not promised.
- Window snapshots render the recorded list to an offscreen texture and perform actual GPU
  readback. This is an explicit synchronization cost for backdrop copies, not the presentation
  path. The extension waits for queue completion with a five-second limit; the pinned Vulkan
  `CommandBuffer::WaitUntilCompleted()` itself is a no-op.
- GPU image cache accounting is bounded to 128 MiB between frames; raster text cache is bounded
  to 512 entries / 32 MiB. In-flight display lists and other renderer/driver allocations are
  additional memory. Render targets retain shared ownership of their command canvas, including
  while an HWND is destroyed.
- This version is experimental. Hardware performance, Steam Deck/Wayland behavior and recovery
  from a real driver/device loss still need target-device validation. The switch is not a claim
  of higher FPS or lower power usage.
- In the visible WSLg preview, the log confirmed the RTX 4070 through Mesa D3D12/OpenGL, and the
  user reported only a few FPS. The cause has not been profiled; investigation was paused for
  comparison with the Skia release. Functional software-driver tests do not resolve this issue.

## Packaging

The build places `libimpeller.so` beside `launchme` and its notices in `licenses/impeller.txt`.
The existing AppImage script bundles the library through linuxdeploy and copies the notices:

```sh
LM_SRC="$PWD" bash tools/linux/make-appimage.sh build/linux-impeller out/impeller
```

Ship the library with an Impeller-enabled executable even when the saved preference is off.
The GPU context is selected at runtime, but the library is a linked build dependency.

## Verification

`impeller_backend_tests` exercises real GL/Vulkan uploads and pixels, BC1/BC3 alpha, invalid
inputs, premultiplied gradients, text, clips/layers, GPU readback, resize and teardown.
`impeller_d2d_tests` covers the application's Direct2D entry points, compressed bitmap creation,
render-target copies and a target outliving its window. CTest uses private Xvfb displays where
available. `LAUNCHME_UI_CAPTURE_DIR` writes PNG fixtures for inspection. Display/context
unavailability is reported as a skipped test, rather than a successful renderer check.

`tools/linux/smoke-impeller.sh` runs the actual launcher with a fresh short-path profile and a
private X server. It supports native captures and clicking the preference followed by restart.
Automated runs should use a network namespace as well as the offline profile, for example on
this WSL machine:

```powershell
wsl -d Ubuntu -u root -- unshare --net -- runuser -u pp -- env `
  LM_SMOKE_SYSTEM_X=100 LM_SMOKE_SYSTEM_Y=448 LM_SMOKE_SCROLL=1 `
  LM_SMOKE_TOGGLE_X=1089 LM_SMOKE_TOGGLE_Y=507 `
  bash /mnt/c/Users/pogor/Desktop/Work/launchme/tools/linux/smoke-impeller.sh `
  /home/pp/lm-impeller/build /tmp/impeller-captures 0 gl desktop
```

The click coordinates describe the script's 1280x800 English desktop fixture, not arbitrary
user windows. Native smoke profiles have stores, companion, news and updates disabled.

### Results (2026-10-31)

- Windows Release build and full CTest: **128/128 passed**.
- Linux builds with and without the Impeller SDK succeeded.
- Full Linux CTest: **67/68 passed**; the remaining case is the existing WebKit failure below.
- Four renderer/integration tests passed on Mesa llvmpipe OpenGL and Vulkan, with BC1/BC3
  actually enabled. These are functional software-driver results, not hardware benchmarks.
- Actual application captures inspected: desktop/settings on OpenGL and TV on Vulkan.
  UI toggles **0 → 1** and **1 → 0** persisted and selected the expected renderer after restart.
- AppImage built with the library and notices. Its bundled AppDir passed the native UI smoke;
  the actual AppImage also completed a 15-second extraction/run smoke with Impeller OpenGL
  and BC1/BC3 enabled (ended by the test's timeout). The loader resolved `libimpeller.so`
  from the package's `usr/lib`, not the SDK build directory.
- The existing `webkit_browser_lifecycle` test fails in this WSL environment. The failure was
  reproduced in the previous `~/lm/build` baseline without Impeller as well; it is not counted
  as a passing test or suppressed by the verification script.

Local experimental artifact: `~/lm-impeller/package/launchme-0.4.6-x86_64.AppImage`
(285,547,000 bytes), SHA-256:
`108eded064c01fb3550f1e150625e5820e340f4e00da4fd712592ee09b0c9b3a`.

Background and the initial API assessment: [Renderer decision](RENDERER_DECISION.md).
