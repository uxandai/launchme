#include "game_controllers.hpp"
#include "pad_rumble.hpp"
#ifdef _WIN32
#include <xinput.h>
#endif
#include <algorithm>
#include <utility>

// Reports and parsers shared by every system; the input readers are per system
// (game_controllers_windows.cpp, game_controllers_linux.cpp).
std::vector<uint8_t> dualSenseRumbleReport(bool bluetooth, bool running, uint8_t sequence) {
    std::vector<uint8_t> bytes(bluetooth ? 78 : 48);
    bytes[0] = bluetooth ? 0x31 : 0x02;
    const size_t offset = bluetooth ? 3 : 1;
    if (bluetooth) { bytes[1] = static_cast<uint8_t>((sequence & 15) << 4); bytes[2] = 0x10; }
    // Compatible rumble emulation, including older firmware. Clearing these bits
    // on stop restores audio haptics (the same protocol used by SDL's PS5 driver).
    bytes[offset] = running ? 0x03 : 0;
    bytes[offset + 2] = running ? 24 : 0; // Light, high-frequency tap.
    bytes[offset + 3] = running ? 12 : 0;
    if (bluetooth) {
        uint32_t crc = 0xffffffff;
        const auto add = [&](uint8_t byte) {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        };
        add(0xa2); // HIDP output header, not included in the transmitted report.
        for (size_t i = 0; i < 74; ++i) add(bytes[i]);
        crc = ~crc;
        for (int i = 0; i < 4; ++i) bytes[74 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    return bytes;
}

PadRumble::PadRumble(Output output) : output_(std::move(output)), worker_([this] { run(); }) {}
PadRumble::~PadRumble() {
    { std::lock_guard lock(mutex_); closing_ = true; ++generation_; pending_.reset(); }
    wake_.notify_one();
    worker_.join();
}
void PadRumble::pulse(Target target) {
    const auto now = std::chrono::steady_clock::now();
    { std::lock_guard lock(mutex_);
      if (closing_ || now < next_pulse_) return;
      next_pulse_ = now + std::chrono::milliseconds(200);
      pending_ = std::move(target); ++generation_; }
    wake_.notify_one();
}
void PadRumble::stop() {
    { std::lock_guard lock(mutex_); pending_.reset(); ++generation_; }
    wake_.notify_one();
}
void PadRumble::run() {
    std::unique_lock lock(mutex_);
    while (!closing_) {
        wake_.wait(lock, [&] { return closing_ || pending_.has_value(); });
        if (closing_) break;
        const auto target = std::move(*pending_); pending_.reset();
        const auto generation = generation_;
        lock.unlock();
        output_(target, true);
        lock.lock();
        wake_.wait_for(lock, std::chrono::milliseconds(55), [&] { return closing_ || generation_ != generation; });
        lock.unlock();
        output_(target, false); // Always the same pad, including focus loss / shutdown.
        lock.lock();
    }
}

PadReading parseDualSense(std::span<const uint8_t> report) {
    PadReading result;
    if (report.empty()) return result;
    const bool bt = report[0] == 0x31;
    if (bt && report.size() < 78) return result;
    if (!bt && report.size() != 10 && report.size() != 64 && report.size() != 78) return result;
    // USB 0x01 is exactly 64 bytes. Windows pads Bluetooth's simple 0x01 report
    // (10 meaningful bytes) to 78: treating it as USB reads a trigger as the hat
    // and turns a neutral pad into a permanently held D-pad Up.
    const bool full = bt || (report[0] == 1 && report.size() == 64);
    const size_t offset = bt ? 2 : 1;
    if ((!bt && report[0] != 1) || report.size() < offset + (full ? 10 : 9)) return result;
    const auto data = report.subspan(offset);
    result.connected = true;
    if (data[full ? 5 : 8] > 128) result.buttons |= pad_right_trigger;
    result.left_trigger = data[full ? 4 : 7] > 128;
    result.x = static_cast<int16_t>((static_cast<int>(data[0]) - 128) * 255);
    result.y = static_cast<int16_t>((128 - static_cast<int>(data[1])) * 255);
    result.rx = static_cast<int16_t>((static_cast<int>(data[2]) - 128) * 255);
    result.ry = static_cast<int16_t>((128 - static_cast<int>(data[3])) * 255);
    const size_t b = full ? 7 : 4;
    const auto hat = data[b] & 15;
    if (hat == 7 || hat == 0 || hat == 1) result.buttons |= XINPUT_GAMEPAD_DPAD_UP;
    if (hat >= 1 && hat <= 3) result.buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (hat >= 3 && hat <= 5) result.buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (hat >= 5 && hat <= 7) result.buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
    // Face buttons by position, like Steam/XInput wrappers: square = X, triangle = Y.
    if (data[b] & 0x10) result.buttons |= XINPUT_GAMEPAD_X;
    if (data[b] & 0x20) result.buttons |= XINPUT_GAMEPAD_A;
    if (data[b] & 0x40) result.buttons |= XINPUT_GAMEPAD_B;
    if (data[b] & 0x80) result.buttons |= XINPUT_GAMEPAD_Y;
    if (data[b + 1] & 1) result.buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (data[b + 1] & 2) result.buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (data[b + 1] & 0x10) result.buttons |= XINPUT_GAMEPAD_BACK;  // Create
    if (data[b + 1] & 0x20) result.buttons |= XINPUT_GAMEPAD_START; // Options
    if (data[b + 2] & 0x01) result.buttons |= pad_guide;            // PS
    if (full) {
        const auto value = data[52];
        const int state = value >> 4, level = value & 15;
        if (state <= 2 && level <= 10) result.battery = state == 2 ? 100 : std::min(100, level * 10 + 5);
        result.charging = state == 1;
    }
    return result;
}

PadReading parseDualShock4(std::span<const uint8_t> report) {
    PadReading result;
    if (report.size() < 10) return result;
    // USB and Bluetooth's simple mode: 0x01 then the data; Bluetooth's full mode: 0x11, two more bytes.
    const size_t offset = report[0] == 0x01 ? 1 : report[0] == 0x11 && report.size() >= 13 ? 3 : 0;
    if (!offset) return result;
    const auto data = report.subspan(offset);
    if (data.size() < 9) return result;
    result.connected = true;
    result.x = static_cast<int16_t>((static_cast<int>(data[0]) - 128) * 255);
    result.y = static_cast<int16_t>((128 - static_cast<int>(data[1])) * 255);
    result.rx = static_cast<int16_t>((static_cast<int>(data[2]) - 128) * 255);
    result.ry = static_cast<int16_t>((128 - static_cast<int>(data[3])) * 255);
    const auto hat = data[4] & 15;
    if (hat == 7 || hat == 0 || hat == 1) result.buttons |= XINPUT_GAMEPAD_DPAD_UP;
    if (hat >= 1 && hat <= 3) result.buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (hat >= 3 && hat <= 5) result.buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (hat >= 5 && hat <= 7) result.buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (data[4] & 0x10) result.buttons |= XINPUT_GAMEPAD_X; // Square
    if (data[4] & 0x20) result.buttons |= XINPUT_GAMEPAD_A; // Cross
    if (data[4] & 0x40) result.buttons |= XINPUT_GAMEPAD_B; // Circle
    if (data[4] & 0x80) result.buttons |= XINPUT_GAMEPAD_Y; // Triangle
    if (data[5] & 0x01) result.buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (data[5] & 0x02) result.buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (data[5] & 0x10) result.buttons |= XINPUT_GAMEPAD_BACK;  // Share
    if (data[5] & 0x20) result.buttons |= XINPUT_GAMEPAD_START; // Options
    if (data[5] & 0x40) result.buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (data[5] & 0x80) result.buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    if (data[6] & 0x01) result.buttons |= pad_guide; // PS
    result.left_trigger = data[7] > 128;
    if (data[8] > 128) result.buttons |= pad_right_trigger;
    return result;
}

