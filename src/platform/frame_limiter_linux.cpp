// Linux has MangoHud and the game's own limiter: nothing is loaded into a game here.
#include "frame_limiter.hpp"

namespace frame_limiter {
bool antiCheatIn(const std::filesystem::path&) { return false; }
struct Controller::Impl {};
Controller::Controller() : impl_(std::make_shared<Impl>()) {}
Controller::~Controller() = default;
void Controller::apply(uint32_t, double, bool, uint32_t) {}
uint64_t Controller::heldMicroseconds(uint32_t) const { return 0; }
void Controller::heartbeat() {}
State Controller::state(uint32_t) const { return State::unsupported; }
void Controller::library(std::filesystem::path) {}
}
