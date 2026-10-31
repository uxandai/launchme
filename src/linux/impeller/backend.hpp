#pragma once
// Optional Linux GPU renderer. Skia types here describe CPU geometry/images, not a Ganesh device.
#include "include/core/SkRefCnt.h"
#include "selection.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>

struct SDL_Window;
class SkCanvas;
class SkImage;

namespace lm_impeller {
bool tryVulkan(bool allowSoftware);
bool tryOpenGl(); // An SDL GL context must be current on this thread.
bool active();   // Only true on the thread owning the context.
std::shared_ptr<SkCanvas> beginFrame(SDL_Window* window, int width, int height);
bool present(SDL_Window* window);
sk_sp<SkImage> snapshot(SkCanvas* canvas);
void releaseWindow(SDL_Window* window);
void shutdown();
bool supportsCompression(unsigned format); // 1: BC1; 3: BC3.
sk_sp<SkImage> compressedImage(unsigned format, int width, int height, const uint8_t* blocks, size_t bytes);
} // namespace lm_impeller
