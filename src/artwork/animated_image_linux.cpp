// Animated artwork is decoded on Windows only so far (animated_image_wic.cpp): here nothing plays
// and the still first frame stands in.
#include "animated_image.hpp"

namespace animated_image {
bool supported() { return false; }
std::optional<Frames> decodeLoop(std::span<const uint8_t>, uint32_t, uint32_t, size_t, size_t, uint32_t) { return std::nullopt; }

struct Player::Shared {};
Player::~Player() = default;
bool Player::open(const std::filesystem::path&, uint32_t, uint32_t) { return false; }
void Player::stop() { shared_.reset(); }
bool Player::failed() const { return false; }
bool Player::frame(std::vector<uint8_t>&, uint32_t&, uint32_t&) { return false; }
} // namespace animated_image
