// The animated WebP container (RIFF): its animation flag, the frames' durations and whole frames of
// a file cut short. Shared by every platform; decoding is animated_image_wic.cpp's.
#include "animated_image.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace animated_image {
namespace {
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t{p[3]} << 24); }
uint32_t le24(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16); }
bool tag(const uint8_t* p, const char* name) { return std::memcmp(p, name, 4) == 0; }
uint32_t shown(uint32_t duration) { return duration <= 10 ? 100 : duration; }

// The chunks after the 12-byte RIFF header, while each one's header fits: visit(tag, offset of its
// payload, payload size) returns false to stop.
template <class Visit>
void chunks(std::span<const uint8_t> bytes, Visit visit) {
    size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const uint32_t size = le32(bytes.data() + at + 4);
        if (!visit(bytes.data() + at, at + 8, size)) return;
        at += 8 + static_cast<size_t>(size) + (size & 1);
    }
}
bool riff(std::span<const uint8_t> bytes) {
    return bytes.size() >= 30 && tag(bytes.data(), "RIFF") && tag(bytes.data() + 8, "WEBP");
}
} // namespace

bool animatedWebp(std::span<const uint8_t> bytes) {
    // VP8X comes first in an extended file: its flags byte has bit 1 for an animation.
    return riff(bytes) && tag(bytes.data() + 12, "VP8X") && le32(bytes.data() + 16) >= 10 && (bytes[20] & 0x02) != 0;
}

std::vector<uint32_t> frameDurations(std::span<const uint8_t> bytes) {
    std::vector<uint32_t> durations;
    if (!animatedWebp(bytes)) return durations;
    chunks(bytes, [&](const uint8_t* header, size_t payload, uint32_t size) {
        if (tag(header, "ANMF") && size >= 16 && payload + 16 <= bytes.size()) durations.push_back(shown(le24(bytes.data() + payload + 12)));
        return true;
    });
    return durations;
}

std::vector<uint32_t> frameDurations(const std::filesystem::path& file) {
    // Only the chunk headers are read: a 30 MB file is not loaded for its timing.
    std::vector<uint32_t> durations;
    std::ifstream input(file, std::ios::binary);
    uint8_t head[30]{};
    if (!input.read(reinterpret_cast<char*>(head), sizeof(head)) || !animatedWebp(head)) return durations;
    uint64_t at = 12;
    for (;;) {
        uint8_t header[8 + 16]{};
        input.clear();
        input.seekg(static_cast<std::streamoff>(at));
        if (!input.read(reinterpret_cast<char*>(header), 8)) break;
        const uint32_t size = le32(header + 4);
        if (tag(header, "ANMF")) {
            if (size < 16 || !input.read(reinterpret_cast<char*>(header + 8), 16)) break;
            durations.push_back(shown(le24(header + 8 + 12)));
        }
        at += 8 + uint64_t{size} + (size & 1);
        if (durations.size() > 100000) break;
    }
    return durations;
}

std::vector<uint8_t> wholeFrames(std::span<const uint8_t> bytes) {
    if (!animatedWebp(bytes)) return {};
    size_t end = 0;
    bool frame = false;
    chunks(bytes, [&](const uint8_t* header, size_t payload, uint32_t size) {
        const size_t next = payload + size + (size & 1);
        if (payload + size > bytes.size()) return false; // Cut inside this chunk.
        if (tag(header, "ANMF")) frame = true;
        end = std::min(next, bytes.size());
        return true;
    });
    if (!frame || end < 30) return {};
    std::vector<uint8_t> result(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(end));
    if (result.size() & 1) result.push_back(0); // A last odd chunk keeps its padding byte.
    const auto riff_size = static_cast<uint32_t>(result.size() - 8);
    for (int i = 0; i < 4; ++i) result[4 + i] = static_cast<uint8_t>(riff_size >> (8 * i));
    return result;
}

size_t Frames::bytes() const {
    size_t sum = 0;
    for (const auto& frame : pixels) sum += frame.size();
    return sum;
}

size_t Frames::at(uint64_t milliseconds) const {
    if (pixels.empty() || !total) return 0;
    uint64_t time = milliseconds % total;
    for (size_t i = 0; i < durations.size() && i < pixels.size(); ++i) {
        if (time < durations[i]) return i;
        time -= durations[i];
    }
    return pixels.size() - 1;
}
} // namespace animated_image
