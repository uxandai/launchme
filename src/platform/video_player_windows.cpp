#include "video_player.hpp"

#ifdef _WIN32
#include <mfapi.h>
#endif
#include <mfmediaengine.h>
#ifdef _WIN32
#include <d3d11.h>
#include <mfidl.h>
#include <wincodec.h>
#endif
#include <wrl/implements.h>

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {
// Errors come on the engine's threads: only a flag crosses over.
class Notify : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFMediaEngineNotify> {
public:
    explicit Notify(std::shared_ptr<std::atomic_bool> error) : error_(std::move(error)) {}
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR, DWORD) override {
        if (event == MF_MEDIA_ENGINE_EVENT_ERROR) error_->store(true);
        return S_OK;
    }
private:
    std::shared_ptr<std::atomic_bool> error_;
};
}

struct VideoPlayer::Impl {
    ComPtr<IMFMediaEngine> engine_;
    // open() with a device: Media Foundation decodes on it (DXVA) and draws frames into textures.
    ComPtr<IMFDXGIDeviceManager> manager_;
    void* device_ = nullptr;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IWICBitmap> bitmap_;
    uint32_t bitmap_width_ = 0, bitmap_height_ = 0;
    bool started_ = false; // MFStartup succeeded.
};

VideoPlayer::VideoPlayer() : impl_(std::make_unique<Impl>()) {}

VideoPlayer::~VideoPlayer() {
    stop();
    if (impl_->started_) MFShutdown();
}

bool VideoPlayer::active() const { return impl_->engine_ != nullptr; }

bool VideoPlayer::open(const std::filesystem::path& file, double volume, void* device) {
    auto& engine_ = impl_->engine_; auto& wic_ = impl_->wic_; auto& started_ = impl_->started_;
    stop();
    error_ = std::make_shared<std::atomic_bool>(false);
    if (!started_) {
        if (FAILED(MFStartup(MF_VERSION))) return false;
        started_ = true;
    }
    if (!wic_ && FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_)))) return false;
    ComPtr<IMFMediaEngineClassFactory> factory;
    ComPtr<IMFAttributes> attributes;
    if (FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(MFCreateAttributes(&attributes, 3))) return false;
    auto notify = Microsoft::WRL::Make<Notify>(error_);
    if (!notify || FAILED(attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify.Get())) ||
        FAILED(attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM))) return false;
    // With the renderer's device: decoded by the GPU's video engine and drawn into its textures
    // (transfer), no frame through memory. Without one (or if the manager cannot take it), copies.
    UINT token = 0;
    ComPtr<IMFDXGIDeviceManager> manager;
    if (device && SUCCEEDED(MFCreateDXGIDeviceManager(&token, manager.GetAddressOf())) &&
        SUCCEEDED(manager->ResetDevice(static_cast<ID3D11Device*>(device), token)) &&
        SUCCEEDED(attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, manager.Get()))) {
        impl_->manager_ = std::move(manager);
        impl_->device_ = device;
    }
    // No playback window: frame-server mode, frames copied to a WIC bitmap or drawn into a texture.
    ComPtr<IMFMediaEngine> engine;
    const auto fail = [&] { if (engine) engine->Shutdown(); impl_->manager_.Reset(); impl_->device_ = nullptr; return false; };
    if (FAILED(factory->CreateInstance(0, attributes.Get(), &engine))) return fail();
    // A byte stream instead of a URL: any local path works, spaces and all.
    ComPtr<IMFByteStream> stream;
    ComPtr<IMFMediaEngineEx> extended;
    if (FAILED(MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, file.c_str(), &stream)) ||
        FAILED(engine.As(&extended))) return fail();
    BSTR name = SysAllocString(file.c_str()); // Its extension tells the engine the container type.
    const HRESULT source = extended->SetSourceFromByteStream(stream.Get(), name);
    SysFreeString(name);
    if (FAILED(source)) return fail();
    engine->SetLoop(TRUE);
    engine->SetVolume(std::clamp(volume, 0.0, 1.0));
    engine->SetMuted(volume <= 0.0);
    if (FAILED(engine->Play())) return fail();
    engine_ = std::move(engine);
    paused_ = false;
    return true;
}

void VideoPlayer::setPaused(bool paused) {
    auto& engine_ = impl_->engine_;
    if (!engine_ || paused == paused_) return;
    if (SUCCEEDED(paused ? engine_->Pause() : engine_->Play())) paused_ = paused;
}

void VideoPlayer::stop() {
    auto& engine_ = impl_->engine_; auto& bitmap_ = impl_->bitmap_;
    auto& bitmap_width_ = impl_->bitmap_width_; auto& bitmap_height_ = impl_->bitmap_height_;
    paused_ = false;
    if (!engine_) return;
    engine_->Pause();
    engine_->Shutdown();
    engine_.Reset();
    impl_->manager_.Reset();
    impl_->device_ = nullptr;
    bitmap_.Reset();
    bitmap_width_ = bitmap_height_ = 0;
}

void* VideoPlayer::device() const { return impl_->engine_ ? impl_->device_ : nullptr; }

bool VideoPlayer::nextFrame(uint32_t& width, uint32_t& height, uint32_t max_width) {
    auto& engine_ = impl_->engine_;
    if (!engine_ || !impl_->device_ || error_->load()) return false;
    LONGLONG time = 0;
    if (engine_->OnVideoStreamTick(&time) != S_OK) return false; // S_FALSE: no new frame.
    DWORD native_width = 0, native_height = 0;
    if (FAILED(engine_->GetNativeVideoSize(&native_width, &native_height)) || !native_width || !native_height) return false;
    width = native_width; height = native_height;
    if (max_width && width > max_width) {
        height = std::max<uint32_t>(1, static_cast<uint32_t>(uint64_t(native_height) * max_width / native_width));
        width = max_width;
    }
    return true;
}

bool VideoPlayer::transfer(void* texture, uint32_t width, uint32_t height) {
    auto& engine_ = impl_->engine_;
    if (!engine_ || !impl_->device_ || !texture || error_->load()) return false;
    const MFVideoNormalizedRect source{0, 0, 1, 1};
    const RECT destination{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const MFARGB border{0, 0, 0, 255};
    return SUCCEEDED(engine_->TransferVideoFrame(static_cast<ID3D11Texture2D*>(texture), &source, &destination, &border));
}

void VideoPlayer::setVolume(double volume) {
    auto& engine_ = impl_->engine_;
    if (!engine_) return;
    engine_->SetVolume(std::clamp(volume, 0.0, 1.0));
    engine_->SetMuted(volume <= 0.0);
}

bool VideoPlayer::frame(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height, uint32_t max_width) {
    auto& engine_ = impl_->engine_; auto& wic_ = impl_->wic_; auto& bitmap_ = impl_->bitmap_;
    auto& bitmap_width_ = impl_->bitmap_width_; auto& bitmap_height_ = impl_->bitmap_height_;
    if (!engine_ || !wic_ || error_->load()) return false;
    LONGLONG time = 0;
    if (engine_->OnVideoStreamTick(&time) != S_OK) return false; // S_FALSE: no new frame.
    DWORD native_width = 0, native_height = 0;
    if (FAILED(engine_->GetNativeVideoSize(&native_width, &native_height)) || !native_width || !native_height) return false;
    uint32_t target_width = native_width, target_height = native_height;
    if (max_width && target_width > max_width) {
        target_height = std::max<uint32_t>(1, static_cast<uint32_t>(uint64_t(native_height) * max_width / native_width));
        target_width = max_width;
    }
    if (!bitmap_ || bitmap_width_ != target_width || bitmap_height_ != target_height) {
        bitmap_.Reset();
        if (FAILED(wic_->CreateBitmap(target_width, target_height, GUID_WICPixelFormat32bppBGRA, WICBitmapCacheOnLoad, &bitmap_))) return false;
        bitmap_width_ = target_width; bitmap_height_ = target_height;
    }
    const MFVideoNormalizedRect source{0, 0, 1, 1};
    const RECT destination{0, 0, static_cast<LONG>(target_width), static_cast<LONG>(target_height)};
    const MFARGB border{0, 0, 0, 255};
    if (FAILED(engine_->TransferVideoFrame(bitmap_.Get(), &source, &destination, &border))) return false;
    const WICRect area{0, 0, static_cast<INT>(target_width), static_cast<INT>(target_height)};
    ComPtr<IWICBitmapLock> lock;
    UINT stride = 0, size = 0;
    BYTE* data = nullptr;
    if (FAILED(bitmap_->Lock(&area, WICBitmapLockRead, &lock)) || FAILED(lock->GetStride(&stride)) ||
        FAILED(lock->GetDataPointer(&size, &data)) || !data || stride < target_width * 4) return false;
    pixels.resize(size_t(target_width) * target_height * 4);
    for (uint32_t row = 0; row < target_height; ++row)
        std::memcpy(pixels.data() + size_t(row) * target_width * 4, data + size_t(row) * stride, size_t(target_width) * 4);
    width = target_width; height = target_height;
    return true;
}
