#include "platform_logo.hpp"
#include "../net/http.hpp"
#include "../platform/atomic_file.hpp"
#include "../library/emulation_platforms.hpp"
#include "../library/studios.hpp"
#include "../platform/text_encoding.hpp"

// Linux: src/linux/win32 (Direct2D on Skia, DirectWrite on SkParagraph, WIC on image_codec).
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <Windows.h>
#include <fstream>
#include <optional>

namespace {
std::wstring caseLogoFilename(const std::wstring& platform) {
    if (platform == L"Switch") return L"nintendo_switch_tall.png";
    if (platform == L"Wii U") return L"nintendo_wiiu_alt.png";
    if (platform == L"Wii") return L"nintendo_wii_alt.png";
    if (platform == L"PS2") return L"playstation_ps2_compact.png";
    if (platform == L"PS3") return L"playstation_ps3_compact.png";
    if (platform == L"PS4") return L"playstation_ps4_compact.png";
    if (platform == L"PS5") return L"playstation_ps5_compact.png";
    return {};
}
std::wstring platformSlug(const std::wstring& platform) {
    const auto* found = findEmulationPlatform(platform);
    return found ? std::wstring(found->slug) : std::wstring{};
}
} // namespace

// Nobody publishes a DRM-free store mark: an open padlock and the word, white on transparent
// like the monochrome store logos (tiles crop logos to their ink). Drawn on the worker thread.
std::vector<uint8_t> drmFreeLogo() {
    using Microsoft::WRL::ComPtr;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<uint8_t> png;
    {
        constexpr UINT width = 860, height = 300;
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICBitmap> bitmap;
        ComPtr<ID2D1Factory> d2d;
        ComPtr<ID2D1RenderTarget> target;
        ComPtr<IDWriteFactory> dwrite;
        ComPtr<IDWriteTextFormat> format;
        ComPtr<ID2D1SolidColorBrush> white;
        ComPtr<ID2D1RoundedRectangleGeometry> body;
        ComPtr<ID2D1EllipseGeometry> hole;
        ComPtr<ID2D1RectangleGeometry> slot;
        ComPtr<ID2D1PathGeometry> keyhole, lock, shackle;
        const auto combine = [&](ID2D1Geometry* a, ID2D1Geometry* b, D2D1_COMBINE_MODE mode, ComPtr<ID2D1PathGeometry>& out) {
            ComPtr<ID2D1GeometrySink> sink;
            return SUCCEEDED(d2d->CreatePathGeometry(out.GetAddressOf())) && SUCCEEDED(out->Open(sink.GetAddressOf())) &&
                   SUCCEEDED(a->CombineWithGeometry(b, mode, nullptr, sink.Get())) && SUCCEEDED(sink->Close());
        };
        const auto arch = [&] { // Right leg in the body, left leg lifted out of it: open.
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(d2d->CreatePathGeometry(shackle.GetAddressOf())) || FAILED(shackle->Open(sink.GetAddressOf()))) return false;
            sink->BeginFigure(D2D1::Point2F(75, 94), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(75, 60));
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(155, 60), D2D1::SizeF(40, 40), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(D2D1::Point2F(155, 134));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            return SUCCEEDED(sink->Close());
        };
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))) &&
            SUCCEEDED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf())) &&
            SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) &&
            SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, target.GetAddressOf())) &&
            SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))) &&
            SUCCEEDED(dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 112, L"en-us", format.GetAddressOf())) &&
            SUCCEEDED(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) &&
            SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), white.GetAddressOf())) &&
            SUCCEEDED(d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(30, 128, 200, 270), 24, 24), body.GetAddressOf())) &&
            SUCCEEDED(d2d->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(115, 186), 17, 17), hole.GetAddressOf())) &&
            SUCCEEDED(d2d->CreateRectangleGeometry(D2D1::RectF(106, 192, 124, 236), slot.GetAddressOf())) &&
            combine(hole.Get(), slot.Get(), D2D1_COMBINE_MODE_UNION, keyhole) &&
            combine(body.Get(), keyhole.Get(), D2D1_COMBINE_MODE_EXCLUDE, lock) && arch()) {
            target->BeginDraw();
            target->Clear(D2D1::ColorF(0, 0, 0, 0));
            target->FillGeometry(lock.Get(), white.Get());
            target->DrawGeometry(shackle.Get(), white.Get(), 24);
            static constexpr wchar_t word[] = L"DRM-FREE";
            target->DrawText(word, static_cast<UINT32>(std::size(word) - 1), format.Get(), D2D1::RectF(236, 60, width, 240), white.Get());
            ComPtr<IStream> stream;
            ComPtr<IWICBitmapEncoder> encoder;
            ComPtr<IWICBitmapFrameEncode> frame;
            STATSTG stats{};
            auto pixel_format = GUID_WICPixelFormat32bppBGRA;
            if (SUCCEEDED(target->EndDraw()) && SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())) &&
                SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) &&
                SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
                SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
                SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&pixel_format)) &&
                SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()) &&
                SUCCEEDED(stream->Stat(&stats, STATFLAG_NONAME)) && stats.cbSize.QuadPart < 16 * 1024 * 1024) {
                // The encoded bytes, read back from the stream's start.
                png.resize(static_cast<size_t>(stats.cbSize.QuadPart));
                LARGE_INTEGER start{};
                ULONG read = 0;
                if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) ||
                    FAILED(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read)) || read != png.size()) png.clear();
            }
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return png;
}

std::wstring platformCaseLogoKey(const std::wstring& platform) {
    return caseLogoFilename(platform).empty() ? platform : platform + L":case";
}

std::wstring platformLogoFilename(const std::wstring& platform) {
    if (platform.ends_with(L":case")) return caseLogoFilename(platform.substr(0, platform.size() - 5));
    if (platform == L"Steam") return L"publisher_steam.png";
    if (platform == L"GOG") return L"publisher_gog.png";
    if (platform == L"Battle.net") return L"publisher_blizzard.png";
    if (platform == L"EA Play") return L"publisher_electronic_arts.png";
    if (platform == L"Ubisoft Connect") return L"publisher_ubisoft_2017.png";
    if (platform == L"Epic Games") return L"publisher_epic_white.png";
    if (platform == L"Xbox") return L"xbox.png";
    const auto* found = findEmulationPlatform(platform);
    return found ? std::wstring(found->logo) : std::wstring{};
}

namespace {
std::vector<uint8_t> logoFile(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
    if (bytes.size() > 128 && bytes.size() < 2 * 1024 * 1024 && bytes[0] == 0x89 && bytes[1] == 'P') return bytes;
    return {};
}
std::vector<uint8_t> backgroundFile(const std::wstring& slug, const std::filesystem::path& cache) {
    for (const auto* extension : {L".png", L".jpg", L".jpeg"}) {
        const auto file = cache / L"backgrounds" / (slug + extension);
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < 128 || input.tellg() > 12 * 1024 * 1024) continue;
        const auto size = static_cast<size_t>(input.tellg());
        input.seekg(0);
        std::vector<uint8_t> bytes(size);
        if (input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)) &&
            ((bytes[0] == 0x89 && bytes[1] == 'P') || (bytes[0] == 0xff && bytes[1] == 0xd8)))
            return bytes;
    }
    return {};
}
} // namespace

// A platform logo shipped in assets\platforms beside the program (Switch 2: not in HVR88's set);
// empty when the program has none of that name.
static std::vector<uint8_t> shippedPlatformLogo(const std::wstring& filename) {
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    return logoFile(std::filesystem::path(exe).parent_path() / L"assets" / L"platforms" / filename);
}

// A publisher's or developer's logo ("publisher:capcom"): shipped in assets\studios beside the program.
std::optional<std::vector<uint8_t>> studioLogo(const std::wstring& platform) {
    if (!platform.starts_with(L"publisher:") && !platform.starts_with(L"developer:")) return std::nullopt;
    const auto parsed = studios::parse(text_encoding::utf8(platform));
    if (!parsed) return std::vector<uint8_t>{};
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    return logoFile(std::filesystem::path(exe).parent_path() / L"assets" / L"studios" / std::wstring(parsed->second->file));
}

std::vector<uint8_t> platformLogoCached(const std::wstring& platform,
                                        const std::filesystem::path& cache) {
    if (auto studio = studioLogo(platform)) return std::move(*studio);
    if (platform == L"DRM-free") return drmFreeLogo();
    const auto filename = platformLogoFilename(platform);
    if (filename.empty()) return {};
    if (auto shipped = shippedPlatformLogo(filename); !shipped.empty()) return shipped;
    return logoFile(cache / filename);
}

std::vector<uint8_t> platformLogo(const std::wstring& platform,
                                  const std::filesystem::path& cache) {
    if (auto studio = studioLogo(platform)) return std::move(*studio);
    if (platform == L"DRM-free") return drmFreeLogo();
    const auto filename = platformLogoFilename(platform);
    if (filename.empty()) return {};
    if (auto shipped = shippedPlatformLogo(filename); !shipped.empty()) return shipped;
    const auto file = cache / filename;
    if (auto bytes = logoFile(file); !bytes.empty()) return bytes;
    // HVR88 has no Epic logo. Use the white shield/transparent background variant.
    const auto url = platform == L"Epic Games" ? std::wstring(L"https://raw.githubusercontent.com/homarr-labs/dashboard-icons/main/png/epic-games-light.png") :
        L"https://raw.githubusercontent.com/HVR88/Monochrome-Gaming-Logos/main/png/" + filename;
    const auto reply = httpGet(url, L"Accept: image/png\r\n", 2 * 1024 * 1024);
    if (reply.status != 200 || reply.body.size() < 128 ||
        reply.body[0] != 0x89 || reply.body[1] != 'P') return {};
    atomic_file::write(file, reply.body);
    return reply.body;
}

std::vector<uint8_t> platformBackground(const std::wstring& platform,
                                        const std::filesystem::path& cache,
                                        bool& logo_fallback) {
    logo_fallback = false;
    const auto slug = platformSlug(platform);
    if (slug.empty()) return {};
    if (auto bytes = backgroundFile(slug, cache); !bytes.empty()) return bytes;
    logo_fallback = true;
    return platformLogo(platform, cache);
}

std::vector<uint8_t> platformBackgroundCached(const std::wstring& platform,
                                              const std::filesystem::path& cache,
                                              bool& logo_fallback) {
    logo_fallback = false;
    const auto slug = platformSlug(platform);
    if (slug.empty()) return {};
    if (auto bytes = backgroundFile(slug, cache); !bytes.empty()) return bytes;
    logo_fallback = true;
    return platformLogoCached(platform, cache);
}
