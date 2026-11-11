// Animated WebP frames through WIC (Windows): its WebP decoder gives every frame already composed on
// the canvas, and asked in order it carries on from the previous one (asking for a far frame first
// composes every frame before it). Container parsing: animated_image.cpp.
#include "animated_image.hpp"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <mutex>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace animated_image {
namespace {
struct Com {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Com() { if (SUCCEEDED(result)) CoUninitialize(); }
};

ComPtr<IWICImagingFactory> factory() {
    ComPtr<IWICImagingFactory> result;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(result.GetAddressOf())))) return {};
    return result;
}

// Inside width x height with the aspect kept, never larger than the picture.
void fit(uint32_t source_w, uint32_t source_h, uint32_t width, uint32_t height, uint32_t& out_w, uint32_t& out_h) {
    const double scale = std::min({1.0, static_cast<double>(width ? width : source_w) / source_w,
                                   static_cast<double>(height ? height : source_h) / source_h});
    out_w = std::max(1u, static_cast<uint32_t>(source_w * scale + 0.5));
    out_h = std::max(1u, static_cast<uint32_t>(source_h * scale + 0.5));
}

// Frame `index` of the decoder at width x height, premultiplied BGRA.
bool frame(IWICImagingFactory* wic, IWICBitmapDecoder* decoder, UINT index, uint32_t width, uint32_t height, std::vector<uint8_t>& pixels) {
    ComPtr<IWICBitmapFrameDecode> decoded;
    UINT stored_w = 0, stored_h = 0;
    if (FAILED(decoder->GetFrame(index, decoded.GetAddressOf())) || FAILED(decoded->GetSize(&stored_w, &stored_h)) || !stored_w || !stored_h) return false;
    ComPtr<IWICBitmapSource> source = decoded;
    if (stored_w != width || stored_h != height) {
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(wic->CreateBitmapScaler(scaler.GetAddressOf())) ||
            FAILED(scaler->Initialize(decoded.Get(), width, height, WICBitmapInterpolationModeFant))) return false;
        source = scaler;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    pixels.resize(size_t(width) * height * 4);
    return pixels.size() <= UINT_MAX &&
           SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
}

// The canvas size of frame 0 and the frame count; false for a still or unreadable file.
bool measure(IWICBitmapDecoder* decoder, UINT& count, uint32_t& width, uint32_t& height) {
    ComPtr<IWICBitmapFrameDecode> first;
    UINT w = 0, h = 0;
    if (FAILED(decoder->GetFrameCount(&count)) || !count || FAILED(decoder->GetFrame(0, first.GetAddressOf())) ||
        FAILED(first->GetSize(&w, &h)) || !w || !h || w > 8192 || h > 8192) return false;
    width = w; height = h;
    return true;
}

uint32_t durationOf(const std::vector<uint32_t>& durations, size_t index) { return index < durations.size() ? durations[index] : 100; }
} // namespace

bool supported() { return true; }

std::optional<Frames> decodeLoop(std::span<const uint8_t> bytes, uint32_t width, uint32_t height,
                                 size_t budget, size_t max_frames, uint32_t min_ms) {
    if (!animatedWebp(bytes) || bytes.size() > MAXDWORD || !max_frames) return std::nullopt;
    const auto durations = frameDurations(bytes);
    Com com;
    const auto wic = factory();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    if (!wic || FAILED(wic->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf()))) return std::nullopt;
    UINT count = 0;
    uint32_t canvas_w = 0, canvas_h = 0;
    if (!measure(decoder.Get(), count, canvas_w, canvas_h)) return std::nullopt;
    Frames result;
    fit(canvas_w, canvas_h, width, height, result.width, result.height);
    const size_t frame_bytes = size_t(result.width) * result.height * 4;
    uint32_t pending = 0; // Since the last kept frame started.
    for (UINT i = 0; i < count; ++i) {
        if (result.pixels.empty() || pending >= min_ms) {
            if (result.pixels.size() >= max_frames || (result.bytes() + frame_bytes > budget && !result.pixels.empty())) break;
            std::vector<uint8_t> pixels;
            if (!frame(wic.Get(), decoder.Get(), i, result.width, result.height, pixels)) break;
            result.pixels.push_back(std::move(pixels));
            result.durations.push_back(0);
            pending = 0;
        }
        const auto duration = durationOf(durations, i);
        result.durations.back() += duration;
        pending += duration;
    }
    if (result.pixels.empty()) return std::nullopt;
    for (const auto duration : result.durations) result.total += duration;
    return result;
}

struct Player::Shared {
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false, fresh = false;
    std::atomic_bool failed{false};
    std::vector<uint8_t> ready;
    uint32_t width = 0, height = 0;
};

namespace {
void play(std::shared_ptr<Player::Shared> shared, std::filesystem::path file, uint32_t width, uint32_t height) {
    const auto fail = [&] { shared->failed.store(true); };
    Com com;
    const auto wic = factory();
    ComPtr<IWICBitmapDecoder> decoder;
    UINT count = 0;
    uint32_t canvas_w = 0, canvas_h = 0;
    if (!wic || FAILED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) ||
        !measure(decoder.Get(), count, canvas_w, canvas_h)) return fail();
    const auto durations = frameDurations(file);
    uint32_t out_w = 0, out_h = 0;
    fit(canvas_w, canvas_h, width, height, out_w, out_h);
    std::vector<uint8_t> back;
    auto due = std::chrono::steady_clock::now();
    for (UINT i = 0;; i = (i + 1) % count) {
        {
            std::lock_guard lock(shared->mutex);
            if (shared->stop) return;
        }
        if (!frame(wic.Get(), decoder.Get(), i, out_w, out_h, back)) return fail();
        std::unique_lock lock(shared->mutex);
        // Shown when due; a single frame waits until stopped.
        if (shared->wake.wait_until(lock, due, [&] { return shared->stop; })) return;
        std::swap(shared->ready, back);
        shared->width = out_w; shared->height = out_h;
        shared->fresh = true;
        if (count == 1) { shared->wake.wait(lock, [&] { return shared->stop; }); return; }
        lock.unlock();
        // Decoding fell behind (a busy machine): the timing starts again from now, without hurrying.
        const auto now = std::chrono::steady_clock::now();
        if (now - due > std::chrono::milliseconds(250)) due = now;
        due += std::chrono::milliseconds(durationOf(durations, i));
    }
}
} // namespace

Player::~Player() { stop(); }

bool Player::open(const std::filesystem::path& file, uint32_t width, uint32_t height) {
    stop();
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) return false;
    auto shared = std::make_shared<Shared>();
    try {
        std::thread(play, shared, file, width, height).detach();
    } catch (...) { return false; }
    shared_ = std::move(shared);
    return true;
}

void Player::stop() {
    if (!shared_) return;
    {
        std::lock_guard lock(shared_->mutex);
        shared_->stop = true;
    }
    shared_->wake.notify_all();
    shared_.reset();
}

bool Player::failed() const { return shared_ && shared_->failed.load(); }

bool Player::frame(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height) {
    if (!shared_) return false;
    std::lock_guard lock(shared_->mutex);
    if (!shared_->fresh) return false;
    shared_->fresh = false;
    pixels.swap(shared_->ready);
    width = shared_->width; height = shared_->height;
    return !pixels.empty();
}
} // namespace animated_image
