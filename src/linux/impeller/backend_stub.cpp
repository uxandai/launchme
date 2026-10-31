#include "backend.hpp"
#include "include/core/SkImage.h"
namespace lm_impeller {
bool compiled() { return false; }
void request(bool) {}
bool requested() { return false; }
bool tryVulkan(bool) { return false; }
bool tryOpenGl() { return false; }
bool active() { return false; }
std::shared_ptr<SkCanvas> beginFrame(SDL_Window*, int, int) { return nullptr; }
bool present(SDL_Window*) { return false; }
sk_sp<SkImage> snapshot(SkCanvas*) { return nullptr; }
void releaseWindow(SDL_Window*) {}
void shutdown() {}
bool supportsCompression(unsigned) { return false; }
sk_sp<SkImage> compressedImage(unsigned, int, int, const uint8_t*, size_t) { return nullptr; }
} // namespace lm_impeller
