// Renderer core: start-up and resize, artwork slots (covers, posters, logos, heroes), the frame
// (paint: every screen and overlay in their layering order), shapes, image state and test capture.
// Drawing lives in the sibling files: renderer_text.cpp, background_renderer.cpp, card_renderer.cpp,
// menus_renderer.cpp, home_chrome_renderer.cpp, friends_renderer.cpp, quick_menu_renderer.cpp,
// grid_renderer.cpp and the per-screen *_renderer.cpp files (declared in renderer.hpp).
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include <dwmapi.h>
#include "carousel.hpp"
#include "category_rail.hpp"
#include "../platform/user_config.hpp"

#include <d2d1helper.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

namespace {
float signedPower(float v, float exponent) { return std::copysign(std::pow(std::abs(v), exponent), v); }
ImageBytes shared(std::vector<uint8_t> bytes) { return shareImage(std::move(bytes)); }
// An empty buffer counts as no image, as with the vector setters.
ImageBytes shared(ImageBytes bytes) { return bytes && !bytes->empty() ? std::move(bytes) : ImageBytes{}; }
constexpr auto reveal_duration = std::chrono::milliseconds(140);
// GPU bytes of an uploaded picture: 4 a pixel, half a byte as BC1, one as BC3 (Kompresja grafik).
size_t bitmapBytes(ID2D1Bitmap* bitmap) {
    if (!bitmap) return 0;
    const auto size = bitmap->GetPixelSize();
    const size_t pixels = static_cast<size_t>(size.width) * size.height;
    const auto format = bitmap->GetPixelFormat().format;
    return format == DXGI_FORMAT_BC1_UNORM ? pixels / 2 : format == DXGI_FORMAT_BC3_UNORM ? pixels : pixels * 4;
}
// Decode caps by drawn size. The largest card (focused shelf card at 4K, launch zoom)
// is ~760 px and the crop preview ~1250 px; posters rarely exceed 600x900 anyway. XMB
// collection thumbnails are drawn at most ~160 px.
constexpr uint32_t cover_short_edge = 1024, thumbnail_short_edge = 256;
// Squircle corner as a fraction of the tile's shorter side: 0.5 is the full superellipse of the
// default shelves; XMB and Flix use smaller corners (posters, capsules and small tiles).
constexpr float full_corner = 0.5f, compact_corner = 0.4f;

// Rectangle with superellipse corners of `radius` (the squircle's exponent) joined by straight
// edges; a square with radius = half its side is the full squircle.
ComPtr<ID2D1PathGeometry> squircleRect(ID2D1Factory* factory, float width, float height, float radius) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (!factory || FAILED(factory->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return {};
    const float r = std::clamp(radius, 0.0f, std::min(width, height) * 0.5f);
    constexpr float quarter = std::numbers::pi_v<float> * 0.5f;
    constexpr int steps = 24; // Per corner: the full squircle's 96 segments.
    const D2D1_POINT_2F centres[] = {{width - r, r}, {width - r, height - r}, {r, height - r}, {r, r}};
    for (int corner = 0; corner < 4; ++corner)
        for (int i = 0; i <= steps; ++i) {
            const float theta = -quarter + corner * quarter + quarter * i / steps;
            const auto point = D2D1::Point2F(centres[corner].x + r * signedPower(std::cos(theta), 0.26f),
                                            centres[corner].y + r * signedPower(std::sin(theta), 0.26f));
            if (!corner && !i) sink->BeginFigure(point, D2D1_FIGURE_BEGIN_FILLED);
            else sink->AddLine(point);
        }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close()) ? path : ComPtr<ID2D1PathGeometry>{};
}
}

bool Renderer::initialize(HWND hwnd, bool layered) {
    layered_ = layered;
    BOOL composed = FALSE;
    immediate_present_ = SUCCEEDED(DwmIsCompositionEnabled(&composed)) && composed;
    wchar_t present[4]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_PRESENT_IMMEDIATE", present, 4) == 1 && (present[0] == L'0' || present[0] == L'1')) immediate_present_ = present[0] == L'1';
#ifdef _WIN32
    // Asked for explicitly (the uncapped benchmark): frames as fast as they come, torn if need be.
    tearing_wanted_ = present[0] == L'1';
    wchar_t option[4]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_SWAPCHAIN", option, 4) == 1 && option[0] == L'0') swap_wanted_ = false;
    if (GetEnvironmentVariableW(L"LAUNCHME_VIDEO_GPU", option, 4) == 1 && option[0] == L'0') gpu_video_ = false;
#endif
    hwnd_ = hwnd;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic_.GetAddressOf())))) return false;
#ifndef LAUNCHME_UI_TEST
    // UI tests decode inline so native snapshots stay deterministic.
    decoder_.start(hwnd);
#endif
    updateViewport();
    loadLocalFonts();
    return createShape() && createFonts();
}

void Renderer::loadLocalFonts() {
    ComPtr<IDWriteFactory3> modern;
    if (FAILED(dwrite_.As(&modern))) return;
    ComPtr<IDWriteFontSetBuilder> builder;
    if (FAILED(modern->CreateFontSetBuilder(builder.GetAddressOf()))) return;
    unsigned loaded = 0;
    const auto fonts = userConfigDirectory() / L"fonts";
    for (const auto* file : {L"SSTRg.TTF", L"SSTLight.TTF", L"SSTMedium.TTF", L"SSTBold.TTF"}) {
        const auto path = fonts / file;
        if (!std::filesystem::exists(path)) continue;
        ComPtr<IDWriteFontFaceReference> reference;
        if (SUCCEEDED(modern->CreateFontFaceReference(path.c_str(), nullptr, 0,
                                                     DWRITE_FONT_SIMULATIONS_NONE, reference.GetAddressOf())) &&
            SUCCEEDED(builder->AddFontFaceReference(reference.Get()))) ++loaded;
    }
    if (!loaded) return;
    ComPtr<IDWriteFontSet> set;
    if (SUCCEEDED(builder->CreateFontSet(set.GetAddressOf())))
        modern->CreateFontCollectionFromFontSet(set.Get(), sst_fonts_.GetAddressOf());
}

void Renderer::resize() {
    updateViewport();
    if (layered_) { discardTarget(); return; }
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    const auto width = static_cast<UINT32>(std::max(1L, rect.right)), height = static_cast<UINT32>(std::max(1L, rect.bottom));
#ifdef _WIN32
    if (swap_) {
        if (!swap_.resize(width, height)) discardTarget();
        return;
    }
#endif
    if (window_target_ && FAILED(window_target_->Resize(D2D1::SizeU(width, height)))) discardTarget();
}

void Renderer::resetCovers(size_t count, bool preserve_background) {
    ++slot_generation_;
    // Preserved: the background layers keep their own bitmaps over the new slots.
    if (preserve_background && selected_ < heroes_.size() && heroes_[selected_].loaded()) {
        kept_hero_ = heroes_[selected_];
        // Library previews use a separate cross-fade. Hand its visible hero to the
        // details/home backdrop, never an unshown prefetched picture or poster.
        if (painted_screen_ == ScreenMode::library && kept_hero_.bitmap &&
            cinematic_hero_ == kept_hero_.bitmap && !samePicture(hero_shown_.pixels, kept_hero_.pixels)) {
            hero_leaving_ = hero_shown_;
            hero_mix_ = std::clamp(std::chrono::duration<float>(std::chrono::steady_clock::now() - cinematic_changed_).count() / cinematic_fade_seconds, 0.0f, 1.0f);
            hero_shown_ = {kept_hero_.pixels, kept_hero_.bitmap, kept_hero_.tone, smoothFade(hero_mix_), kept_hero_.focus};
            if (hero_mix_ >= 1) hero_leaving_ = {};
        }
    } else if (!preserve_background) {
        kept_hero_ = {};
        hero_shown_ = {}; hero_leaving_ = {}; hero_mix_ = 1; hero_detail_seen_ = 0;
        cinematic_hero_.Reset(); cinematic_previous_hero_.Reset();
        library_backdrop_active_ = false;
        library_backdrop_from_ = library_backdrop_opacity_ = 0;
    }
    covers_.clear(); covers_.resize(count);
    logos_.clear(); logos_.resize(count);
    heroes_.clear(); heroes_.resize(count);
    posters_.clear(); posters_.resize(count);
    discs_.clear(); discs_.resize(count);
    tile_thumbnails_.clear(); tile_thumbnails_.resize(count);
}

void Renderer::remapCovers(const std::vector<size_t>& from, bool preserve_background) {
    if (!preserve_background) { hero_shown_ = {}; hero_leaving_ = {}; kept_hero_ = {}; hero_mix_ = 1; hero_detail_seen_ = 0; }
    const auto remap = [&](auto& list) {
        std::remove_reference_t<decltype(list)> next(from.size());
        for (size_t i = 0; i < from.size(); ++i) if (from[i] < list.size()) next[i] = std::move(list[from[i]]);
        list = std::move(next);
    };
    remap(covers_); remap(logos_); remap(heroes_); remap(posters_); remap(discs_); remap(tile_thumbnails_);
    ++slot_generation_;
}

void Renderer::keepCovers(size_t keep, size_t count) {
    ++slot_generation_;
    for (auto* list : {&covers_, &logos_, &heroes_, &posters_, &discs_}) { list->resize(std::min(keep, list->size())); list->resize(count); }
    tile_thumbnails_.resize(std::min(keep, tile_thumbnails_.size())); tile_thumbnails_.resize(count);
}

void Renderer::setTileThumbnail(size_t index, int slot, ImageBytes pixels, CropFocus focus) {
    if (index >= tile_thumbnails_.size() || slot < 0 || slot >= 4) return;
    auto& thumbnail = tile_thumbnails_[index][slot];
    pixels = shared(std::move(pixels));
    // The bytes already bound (a kept tile rebinding its preview): no decode or fade again.
    if (thumbnail.same(pixels)) { rebindPicture(thumbnail, pixels); thumbnail.focus = focus; createBitmap(thumbnail); return; }
    bindPicture(thumbnail, std::move(pixels));
    thumbnail.decoding.reset();
    thumbnail.focus = focus;
    thumbnail.short_edge = thumbnailEdge();
    thumbnail.bitmap.Reset();
    thumbnail.brush.Reset();
    createBitmap(thumbnail);
}

void Renderer::setCover(size_t index, ImageBytes pixels, CropFocus focus, uint32_t short_edge) {
    if (index >= covers_.size()) return;
    Cover& cover = covers_[index];
    pixels = shared(std::move(pixels));
    const bool loose = short_edge != 0;
    if (!short_edge) short_edge = coverEdge();
    // The bytes already bound (a rebind of the same art): keep the decoded bitmap, no fade again.
    // A bitmap decoded larger for a larger draw (wantEdge) stays as it is.
    if (cover.same(pixels)) {
        rebindPicture(cover, pixels); cover.focus = focus; cover.loose_edge = loose;
        redecode(cover, low_memory_ && !loose ? std::max(short_edge, cover.short_edge) : short_edge);
        return;
    }
    bindPicture(cover, std::move(pixels));
    cover.decoding.reset();
    cover.focus = focus;
    cover.short_edge = short_edge;
    cover.loose_edge = loose;
    cover.bitmap.Reset();
    cover.brush.Reset();
    cover.gray_bitmap.Reset();
    cover.gray_brush.Reset();
    cover.gray_failed = false;
    createBitmap(cover);
}

void Renderer::setPoster(size_t index, ImageBytes pixels, uint32_t short_edge) {
    if (index >= posters_.size()) return;
    Cover& poster = posters_[index];
    pixels = shared(std::move(pixels));
    const bool loose = short_edge != 0;
    if (!short_edge) short_edge = coverEdge();
    // Rebinding the bytes already shown (another row's copy of the game, a rebuilt row) must not
    // drop the bitmap: it would decode and fade in again.
    if (poster.same(pixels)) {
        rebindPicture(poster, pixels); poster.loose_edge = loose;
        redecode(poster, low_memory_ && !loose ? std::max(short_edge, poster.short_edge) : short_edge);
        return;
    }
    poster = {};
    bindPicture(poster, std::move(pixels));
    poster.short_edge = short_edge;
    poster.loose_edge = loose;
    createBitmap(poster);
}

// The same bytes wanted at another decode size: decoded again, the shown bitmap stays meanwhile.
void Renderer::redecode(Cover& cover, uint32_t short_edge) {
    if (cover.short_edge != short_edge && cover.loaded() && target_) {
        cover.short_edge = short_edge;
        if (cover.bitmap || cover.decoding) { // A decode at the old size is dropped; the shown bitmap stays.
            auto bytes = cover.encoded();
            if (!bytes) return; // Low memory: nobody keeps the bytes; the shown bitmap stays at its size.
            cover.decoding = decoder_.submit(std::move(bytes), decodeKind(cover));
            if (cover.decoding->done.load(std::memory_order_acquire)) upload(cover, false);
            return;
        }
    }
    createBitmap(cover);
}

void Renderer::rankArt(size_t index, int rank) {
    // Pending decodes take it now; finished ones waiting for upload use it for their order.
    const auto set = [](Cover& cover, int priority) { if (cover.decoding) cover.decoding->priority.store(priority, std::memory_order_relaxed); };
    if (index < posters_.size()) set(posters_[index], 1 + rank * 2);
    if (index < covers_.size()) set(covers_[index], 2 + rank * 2);
}

size_t Renderer::artBytes(size_t index) const {
    size_t bytes = 0;
    for (const auto* list : {&posters_, &covers_})
        if (index < list->size()) bytes += bitmapBytes((*list)[index].bitmap.Get());
    return bytes;
}

bool Renderer::artPending(size_t index) const {
    for (const auto* list : {&covers_, &posters_, &discs_})
        if (index < list->size() && (*list)[index].pixels) return true;
    return false;
}

// The largest card of Tryb TV's layouts is about 320 DIPs on its shorter side (the focused shelf
// card); a card drawn larger (the launch zoom, Szczegóły, the crop editor) raises its own cap.
uint32_t Renderer::coverEdge() const {
    if (!low_memory_) return cover_short_edge;
    const auto edge = static_cast<uint32_t>(std::ceil(320.0f * viewport_.scale / 64.0f)) * 64;
    return std::clamp(edge, 256u, cover_short_edge);
}
uint32_t Renderer::thumbnailEdge() const { // XMB's collection previews: about 72 DIPs.
    if (!low_memory_) return thumbnail_short_edge;
    const auto edge = static_cast<uint32_t>(std::ceil(72.0f * viewport_.scale / 32.0f)) * 32;
    return std::clamp(edge, 96u, thumbnail_short_edge);
}

void Renderer::wantEdge(Cover& cover, float magnify) {
    if (!low_memory_ || magnify < 1.3f || cover.decoding || !cover.bitmap || !cover.short_edge || cover.hero || cover.logo) return;
    const auto size = cover.bitmap->GetPixelSize();
    const uint32_t shorter = std::min(size.width, size.height);
    // Smaller than its cap (less the BC1 block rounding): the source itself is no larger.
    if (shorter + 4 < cover.short_edge || cover.short_edge >= cover_short_edge) return;
    const auto wanted = static_cast<uint32_t>(std::ceil(static_cast<float>(shorter) * magnify / 64.0f)) * 64;
    const uint32_t edge = std::min(wanted, cover_short_edge);
    if (edge <= cover.short_edge) return;
    auto bytes = cover.encoded();
    if (!bytes) return; // Nobody keeps the bytes now: the bitmap stays as it is.
    // Uploaded with the next frame's finished decodes, never here: this bitmap is being drawn.
    cover.short_edge = edge;
    cover.decoding = decoder_.submit(std::move(bytes), decodeKind(cover));
}

void Renderer::releaseGraphics() {
    discardTarget();
    const auto empty = [&](Cover& cover) { if (cover.loaded()) art_lost_ = true; cover = {}; };
    for (auto* list : {&covers_, &logos_, &heroes_, &posters_, &discs_}) for (auto& cover : *list) empty(cover);
    for (auto& tile : tile_thumbnails_) for (auto& thumbnail : tile) empty(thumbnail);
    kept_hero_ = {};
    for (auto& [id, image] : companion_feed_images_) image.decoding.reset();
    text_layouts_.clear();
    type_metrics_.clear();
    // The factory keeps its Direct3D device while anything it made lives: its geometries and stroke
    // styles go too, and createTarget makes a new factory and the shapes again.
    capped_shapes_.clear(); compact_shapes_.clear(); brand_icons_.clear(); controller_art_.clear(); poster_shapes_.clear();
    squircle_.Reset(); squircle_compact_.Reset(); power_arc_.Reset(); controller_glyph_.Reset(); play_glyph_.Reset();
    controller_art_stroke_.Reset(); companion_stroke_.Reset(); tarot_stroke_.Reset();
    factory_.Reset();
#ifdef _WIN32
    swap_failed_ = false; // The next target tries the swap chain again.
#endif
}

void Renderer::setDisc(size_t index, ImageBytes pixels) {
    if (index >= discs_.size()) return;
    auto& disc = discs_[index]; pixels = shared(std::move(pixels));
    if (disc.same(pixels)) { rebindPicture(disc, pixels); createBitmap(disc); return; }
    disc = {}; bindPicture(disc, std::move(pixels)); disc.short_edge = coverEdge();
    createBitmap(disc);
}

void Renderer::setPlatformCover(size_t index, ImageBytes pixels) {
    if (index >= covers_.size()) return;
    auto& cover = covers_[index];
    cover.logo = true; // Crop transparent gutters before fitting the logo inside the tile.
    cover.short_edge = 0;
    bindPicture(cover, shared(std::move(pixels)));
    cover.decoding.reset();
    cover.bitmap.Reset();
    cover.brush.Reset();
    createBitmap(cover);
}

void Renderer::setLogo(size_t index, ImageBytes pixels) {
    if (index >= logos_.size()) return;
    Cover& logo = logos_[index];
    if ((logo.bitmap || logo.decoding) && logo.loaded()) return;
    logo.logo = true;
    bindPicture(logo, shared(std::move(pixels)));
    logo.decoding.reset();
    logo.bitmap.Reset();
    logo.brush.Reset();
    createBitmap(logo);
}

void Renderer::setHeroFocus(size_t index, CropFocus focus) {
    if (index >= heroes_.size()) return;
    heroes_[index].focus = focus;
    if (heroes_[index].pixels && samePicture(heroes_[index].pixels, hero_shown_.pixels)) hero_shown_.focus = focus;
}

void Renderer::setHero(size_t index, ImageBytes pixels, HeroTone tone, bool force, CropFocus focus) {
    if (index >= heroes_.size()) return;
    Cover& hero = heroes_[index];
    if (!force && (hero.bitmap || hero.decoding) && hero.loaded()) {
        if (pixels && samePicture(pixels, hero.pixels)) setHeroFocus(index, focus); // A crop saved meanwhile.
        return;
    }
    pixels = shared(std::move(pixels));
    if (pixels && samePicture(pixels, hero.pixels)) {
        hero.tone = tone;
        setHeroFocus(index, focus);
        createBitmap(hero);
        return;
    }
    if (pixels && samePicture(pixels, kept_hero_.pixels)) {
        hero = kept_hero_;
        hero.tone = tone;
        setHeroFocus(index, focus);
        createBitmap(hero);
        return;
    }
    hero.hero = true;
    hero.tone = tone;
    hero.focus = focus;
    hero.pixels = std::move(pixels);
    hero.decoding.reset();
    hero.bitmap.Reset();
    hero.brush.Reset();
    if (target_ && hero.pixels) {
        ID2D1Bitmap* existing = samePicture(hero.pixels, hero_shown_.pixels) ? hero_shown_.bitmap.Get() :
                              samePicture(hero.pixels, hero_leaving_.pixels) ? hero_leaving_.bitmap.Get() : nullptr;
        if (existing) {
            hero.bitmap = existing;
            target_->CreateBitmapBrush(hero.bitmap.Get(), hero.brush.GetAddressOf());
            if (hero.brush) hero.brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
    }
    createBitmap(hero);
}

void Renderer::setFriendAvatar(uint64_t id, std::vector<uint8_t> pixels) {
    auto& avatar = avatars_[id];
    avatar.hero = true;
    avatar.pixels = shared(std::move(pixels));
    avatar.decoding.reset();
    avatar.bitmap.Reset();
    avatar.brush.Reset(); // upload() fills it through GetAddressOf: an old one would leak.
    createBitmap(avatar);
}

void Renderer::clearFriendAvatars() { avatars_.clear(); }

void Renderer::setPluginThumbnail(size_t index, ImageBytes pixels, CropFocus focus) {
    if (index >= plugin_thumbnails_.size()) return;
    auto& image = plugin_thumbnails_[index];
    if (image.pixels == pixels) return;
    image = {}; image.pixels = std::move(pixels); image.focus = focus;
    if (image.pixels) createBitmap(image);
}

void Renderer::setPlatformBadge(const std::wstring& platform, std::vector<uint8_t> pixels) {
    auto& badge = platform_badges_[platform];
    if (badge.loaded() ? *badge.pixels == pixels : pixels.empty()) return;
    badge = {};
    badge.logo = true;
    badge.pixels = shared(std::move(pixels));
    createBitmap(badge);
}

void Renderer::paint(const std::vector<GameCard>& cards, size_t selected, bool scanning,
                    float elapsed, float detail, const ViewContext& context) {
    rating_link_ = {};
    companion_bubble_ = {}; // Set again by whichever tile of hers shows her line this frame.
    companion_links_.clear();
    companion_videos_.clear();
    marquee_active_ = false; // Set again by a gliding name (fadingLine).
    const auto started = std::chrono::steady_clock::now();
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    if (createTarget()) {
        updateViewport();
        const bool exit_snapshot = context.exit_progress >= 0 && exit_frame_;
        if (!exit_snapshot) uploadDecoded();
        if (exit_snapshot || (context.screensaver && context.screensaver->opacity >= 0.999f)) {
            // A captured exit frame or opaque screensaver covers everything. Skip
            // the launcher scene and leave its selection/hero handoff untouched.
            context_ = &context;
            beginFrame();
            target_->BeginDraw();
            target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
            target_->Clear(color(0, 0, 0));
            if (!exit_snapshot) drawScreensaver(*context.screensaver);
            if (context.exit_progress >= 0) drawExitTransition(context.exit_progress);
            const auto submitted = std::chrono::steady_clock::now();
            const auto result = finishFrame(target_->EndDraw());
            if (profiling_) {
                timing_ = {std::chrono::duration<double, std::milli>(submitted - started).count(),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitted).count(), pending_upload_ms_, pending_uploads_};
                pending_upload_ms_ = 0; pending_uploads_ = 0;
            }
            if (result == D2DERR_RECREATE_TARGET) discardTarget();
            context_ = nullptr;
            EndPaint(hwnd_, &ps);
            return;
        }
        cards_ = &cards;
        const bool library_backdrop = context.screen == ScreenMode::shelves && !scanning &&
            selected < cards.size() && cards[selected].game.kind == GameKind::all;
        // Snapshot the current mix on interrupted navigation, including a rebuilt
        // shelf at the same index. The app's detail clock also works while unfocused.
        if (library_backdrop != library_backdrop_active_ || selected != selected_ || detail < detail_)
            library_backdrop_from_ = library_backdrop_opacity_;
        library_backdrop_active_ = library_backdrop;
        library_backdrop_opacity_ = std::lerp(library_backdrop_from_, library_backdrop ? 1.0f : 0.0f, smoothFade(detail));
        context_ = &context;
        desktop_style_ = context.desktop_window;
        desktop_modal_ = desktop_ui::DialogKind::none; // Set again by the desktop dialog drawn this frame.
        desktop_modal_hotspots_.clear();
        selected_ = selected;
        screen_changed_ = painted_screen_ != context.screen;
        painted_screen_ = context.screen;
        scanning_ = scanning;
        if (!scanning) scanning_since_ = {};
        else if (scanning_since_ == std::chrono::steady_clock::time_point{}) scanning_since_ = std::chrono::steady_clock::now();
        elapsed_ = elapsed;
        detail_ = detail;
        beginFrame();
        target_->BeginDraw();
        const float scale = viewport_.scale;
        target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
        target_->Clear(color(0.025f, 0.030f, 0.036f));
        if (context.launch_animation_seconds >= 0) {
            drawBackground();
            drawLaunchAnimation();
        } else if (context.desktop) { drawDesktop(); captureDesktopBackdrop(); }
        else if (context.desktop_sheet && context.screen == ScreenMode::settings) drawDesktopSettings();
        else if (context.desktop_sheet && context.screen == ScreenMode::details) drawDesktopDetails();
        else if (context.desktop_sheet && context.screen == ScreenMode::artwork) drawDesktopArtwork();
        else if (context.desktop_sheet && context.screen == ScreenMode::downloads) drawDesktopDownloadsPage();
        else if (context.desktop_sheet && context.screen == ScreenMode::glance) drawDesktopGlance();
        else if (context.desktop_sheet && context.screen == ScreenMode::store) drawDesktopStore();
        else if (context.desktop_embed) drawDesktopEmbedded();
        else if (context.screen == ScreenMode::settings) drawSettings();
        else if (context.screen == ScreenMode::plugin_page) drawPluginPage();
        else if (context.screen == ScreenMode::downloads) drawDownloads();
        else if (context.screen == ScreenMode::glance) drawGlance();
        else if (context.screen == ScreenMode::companion) drawCompanion();
        else if (context.screen == ScreenMode::artwork) drawArtworkBrowser();
        else if (context.screen == ScreenMode::details) { drawDetails(); if (context.achievements) drawAchievements(); }
        else if (context.screen == ScreenMode::devices) drawDevices();
        else if (context.screen == ScreenMode::crop) drawCropEditor();
        else if (gridScreen(context.screen)) {
            drawGrid();
        } else if (context.snitch) {
            drawSnitch();
        } else if (context.flix && !scanning_ && !cards.empty()) {
            squircle_corner_ = compact_corner;
            drawFlix();
            squircle_corner_ = full_corner;
        } else if (context.metro && !scanning_ && !cards.empty()) {
            squircle_corner_ = compact_corner;
            corner_reference_ = metro_layout::cell_h; // The large tiles keep the small ones' corners.
            drawMetro();
            corner_reference_ = 0;
            squircle_corner_ = full_corner;
        } else if (context.psfive && !scanning_ && !cards.empty()) {
            squircle_corner_ = compact_corner;
            drawPsfive();
            squircle_corner_ = full_corner;
        } else {
            if (context.xmb) squircle_corner_ = compact_corner; // Cards only; drawers and panels keep the full shape.
            if (context.shelf_posters) {
                // Posters: round corners kept round (not the square card's squircle stretched), the
                // large focused one no rounder than the small ones.
                squircle_corner_ = compact_corner;
                corner_reference_ = 140;
            }
            drawBackground();
            beginScreenForeground();
            if (!context.xmb) drawHero(); // XMB labels sit next to the tiles instead.
            drawQuickShade();
            if (context.category_rail)
                target_->PushAxisAlignedClip(D2D1::RectF(category_rail::Layout{viewport_.width, viewport_.height}.contentClip(),
                    0, viewport_.width, viewport_.height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if (scanning_ || cards.empty()) drawEmptyState();
            else if (context.xmb) drawXmb();
            else {
                for (size_t i = 0; i < cards.size(); ++i) if (i != selected_) drawCard(cards[i], i);
                const float focus = smoothFade(context.launch_progress);
                if (focus > 0.001f) {
                    brush(color(0.008f, 0.012f, 0.016f, focus * 0.82f));
                    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
                }
                drawCard(cards[selected_], selected_);
            }
            if (context.category_rail) target_->PopAxisAlignedClip();
            squircle_corner_ = full_corner;
            corner_reference_ = 0;
        }
        if ((context.desktop || context.desktop_sheet) && context.launch_animation_seconds < 0) {
            // Tryb TV's menus, drawers and dialogs over a desktop page: the same layouts (and hit
            // tests, through viewport()) on a compact canvas.
            viewport_ = desktopModalViewport();
            target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
        }
        HRESULT foreground_result = S_OK;
        if (context.launch_animation_seconds < 0) {
            if (!context.desktop && !context.desktop_sheet && !context.desktop_embed) { // Desktop mode draws its own chrome (desktop_renderer.cpp).
                drawFooter();
                drawCategoryRail();
                drawPlatformBadge();
                drawLibraryControls();
                if (context.flix_icons && context.screen == ScreenMode::shelves) drawFlixIcons();
                if (context.glance_shortcut) drawGlanceShortcut();
                if (context.companion && (context.companion->tile || context.companion->pill)) drawCompanionTile();
            }
            foreground_result = endScreenForeground();
            drawContextMenu();
            if (!context.snitch) drawQuickPanel(); // snitch's permanent dock belongs below overlays.
            drawCategoryMenu();
            drawFriends();
            drawGogInstall();
            drawBulkCovers();
            drawCustomGameEditor();
            if (context.tv_meta) drawDesktopMeta(*context.tv_meta); // Edytuj metadane from the game's menu.
            if (context.identify) drawIdentify(); // Over Dodaj / Edytuj grę, which opens it.
            drawLibraryManager();
            // Opened from the panels above; the power menu and exit prompt stay over it (over Aurelia, below).
            if (!context.aurelia) { drawFileBrowser(); drawKeyboard(); } // Aurelia's field belongs above its page.
            drawCompanionPicker();
            drawProfile(); // Over any screen, from the drawer: the lowest dialog.
            drawTarot(); // Over the home it opens from; the walkthrough, prompts and toasts above it.
            drawAurelia();
            if (context.aurelia) { drawFileBrowser(); drawKeyboard(); } // Inny folder… opens the picker over it.
            drawWhatsNew(); // After an update's restart: the top dialog; the walkthrough and prompts above it.
            drawTour(); // Over the panels it opens; notifications and the exit prompt stay above it.
            drawHints();
            // Wallpaper screens animate only their foreground (above); opaque panels
            // retain the whole-screen entrance. A failed foreground target stays live.
            const bool wallpaper = context.desktop_sheet || context.desktop_embed || context.screen == ScreenMode::shelves || context.screen == ScreenMode::platform_games ||
                                   context.screen == ScreenMode::details || context.screen == ScreenMode::glance ||
                                   context.screen == ScreenMode::companion || gridScreen(context.screen);
            if (!wallpaper && context.screen_transition < 0.999f) drawScreenTransition(context.screen_transition);
            drawNotification();
            drawVolumeIndicator();
            drawExitPrompt();
            drawWindowStrip();
            if (!context.performance.empty()) {
                brush(color(0.015f, 0.02f, 0.025f, 0.94f));
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(viewport_.width - 676, 20, viewport_.width - 20, 144), 10, 10), solid_.Get());
                text(context.performance, viewport_.width - 658, 31, 620, 110, settings_body_font_.Get(), color(0.92f, 0.96f, 0.94f));
            }
            if (context.screensaver) { // Dimming in above the launcher screen.
                target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
                drawScreensaver(*context.screensaver);
            }
        }
        if (context.exit_progress >= 0) drawExitTransition(context.exit_progress);
        const auto submitted = std::chrono::steady_clock::now();
        const auto result = finishFrame(target_->EndDraw());
#ifdef LAUNCHME_UI_TEST
        draw_result_for_test = FAILED(foreground_result) ? foreground_result : result;
#endif
        if (profiling_) {
            timing_ = {std::chrono::duration<double, std::milli>(submitted - started).count(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitted).count(), pending_upload_ms_, pending_uploads_};
            pending_upload_ms_ = 0; pending_uploads_ = 0;
        }
        if (result == D2DERR_RECREATE_TARGET || foreground_result == D2DERR_RECREATE_TARGET) discardTarget();
        else if (screen_changed_) releaseIdleCaches();
        cards_ = nullptr;
        context_ = nullptr;
    }
    EndPaint(hwnd_, &ps);
}

bool Renderer::createShape() {
    if (FAILED(factory_->CreatePathGeometry(squircle_.GetAddressOf()))) return false;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(squircle_->Open(sink.GetAddressOf()))) return false;
    constexpr int segments = 96;
    for (int i = 0; i <= segments; ++i) {
        const float theta = -std::numbers::pi_v<float> * 0.5f + 2.0f * std::numbers::pi_v<float> * i / segments;
        const auto point = D2D1::Point2F(0.5f + 0.5f * signedPower(std::cos(theta), 0.26f),
                                        0.5f + 0.5f * signedPower(std::sin(theta), 0.26f));
        if (!i) sink->BeginFigure(point, D2D1_FIGURE_BEGIN_FILLED);
        else sink->AddLine(point);
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return false;
    sink.Reset();
    squircle_compact_ = squircleRect(factory_.Get(), 1, 1, compact_corner);
    if (FAILED(factory_->CreatePathGeometry(controller_glyph_.GetAddressOf())) || FAILED(controller_glyph_->Open(sink.GetAddressOf()))) return false;
    sink->BeginFigure(D2D1::Point2F(0.25f, 0.25f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment({0.35f, 0.18f}, {0.65f, 0.18f}, {0.75f, 0.25f}));
    sink->AddBezier(D2D1::BezierSegment({0.90f, 0.20f}, {0.94f, 0.30f}, {0.97f, 0.72f}));
    sink->AddBezier(D2D1::BezierSegment({1.0f, 0.94f}, {0.83f, 0.96f}, {0.72f, 0.72f}));
    sink->AddLine({0.65f, 0.60f}); sink->AddLine({0.35f, 0.60f}); sink->AddLine({0.28f, 0.72f});
    sink->AddBezier(D2D1::BezierSegment({0.17f, 0.96f}, {0.0f, 0.94f}, {0.03f, 0.72f}));
    sink->AddBezier(D2D1::BezierSegment({0.06f, 0.30f}, {0.10f, 0.20f}, {0.25f, 0.25f}));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return false;
    sink.Reset();
    if (FAILED(factory_->CreatePathGeometry(play_glyph_.GetAddressOf())) || FAILED(play_glyph_->Open(sink.GetAddressOf()))) return false;
    // Optically centred: the centroid of the triangle sits on the origin.
    sink->BeginFigure(D2D1::Point2F(-0.34f, -0.5f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine({0.56f, 0.0f}); sink->AddLine({-0.34f, 0.5f});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close());
}

#ifdef LAUNCHME_UI_TEST
bool Renderer::captureForTest(const std::filesystem::path& file, const std::function<void()>& draw) {
    discardTarget();
    if (!ensureFactory()) return false;
    RECT bounds{};
    GetClientRect(hwnd_, &bounds);
    const auto width = static_cast<UINT>(std::max(1L, bounds.right));
    const auto height = static_cast<UINT>(std::max(1L, bounds.bottom));
    ComPtr<IWICBitmap> bitmap;
    if (FAILED(wic_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf())) ||
        FAILED(factory_->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE),
                                                     target_.GetAddressOf())) || !createTargetResources()) return false;
    draw();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    auto format = GUID_WICPixelFormat32bppBGRA;
    const bool saved = SUCCEEDED(wic_->CreateStream(stream.GetAddressOf())) &&
        SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic_->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
        SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    discardTarget();
    return saved;
}
#endif

float Renderer::reveal(const Cover& cover) const {
    if (cover.revealed == std::chrono::steady_clock::time_point{}) return 1.0f;
    const auto age = std::chrono::steady_clock::now() - cover.revealed;
    return age >= reveal_duration ? 1.0f : smoothFade(std::chrono::duration<float>(age) / reveal_duration);
}

void Renderer::setVideoFrame(const uint8_t* bgra, uint32_t width, uint32_t height) {
    if (!target_ || !bgra || !width || !height) return;
    const auto size = video_bitmap_ ? video_bitmap_->GetPixelSize() : D2D1_SIZE_U{};
    if (video_bitmap_ && size.width == width && size.height == height) {
        video_bitmap_->CopyFromMemory(nullptr, bgra, width * 4);
        return;
    }
    const bool first = !video_bitmap_;
    video_bitmap_.Reset();
    target_->CreateBitmap(D2D1::SizeU(width, height), bgra, width * 4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), video_bitmap_.GetAddressOf());
    if (first) video_started_ = std::chrono::steady_clock::now();
}

void Renderer::clearVideo() {
    video_bitmap_.Reset();
#ifdef _WIN32
    video_texture_.Reset(); video_texture_bitmap_.Reset();
#endif
}

bool Renderer::animating() const {
    return decoder_.pending() > 0 || decoder_.completed() != decoded_seen_ || uploads_left_ ||
           std::chrono::steady_clock::now() - cinematic_changed_ < std::chrono::milliseconds(350) || cinematic_waiting_ ||
           hero_mix_ < 1 || hero_waiting_ ||
           (desktop_viewport_ && std::chrono::steady_clock::now() - desktop_hero_changed_ < std::chrono::milliseconds(320)) ||
           std::chrono::steady_clock::now() - last_reveal_ < reveal_duration ||
           std::chrono::steady_clock::now() - skeleton_at_ < std::chrono::milliseconds(150);
}

void Renderer::drawSkeleton(D2D1_RECT_F area, int lines, float step, float alpha) {
    const float width = area.right - area.left;
    if (width <= 0 || lines <= 0 || alpha <= 0.003f) return;
    skeleton_at_ = std::chrono::steady_clock::now();
    const float bar = std::min(12.0f, step * 0.5f);
    std::vector<D2D1_ROUNDED_RECT> bars;
    for (int i = 0; i < lines; ++i) {
        const float top = area.top + i * step + (step - bar) * 0.5f;
        if (top + bar > area.bottom) break;
        const float share = i == lines - 1 ? 0.58f : i % 3 == 1 ? 0.9f : 0.97f; // The last line shorter, as a paragraph ends.
        bars.push_back(D2D1::RoundedRect(D2D1::RectF(area.left, top, area.left + width * share, top + bar), bar * 0.5f, bar * 0.5f));
    }
    brush(palette::gray(1, 0.06f * alpha));
    for (const auto& r : bars) target_->FillRoundedRectangle(r, solid_.Get());
    // The sweep: a band of light crossing the whole block every ~1.6 s.
    const float band = std::max(120.0f, width * 0.35f);
    const float phase = std::fmod(elapsed_ / 1.6f, 1.0f);
    const float center = area.left - band + (width + 2 * band) * phase;
    const D2D1_GRADIENT_STOP stops[] = {{0.0f, D2D1::ColorF(1, 1, 1, 0)}, {0.5f, D2D1::ColorF(1, 1, 1, 0.07f * alpha)}, {1.0f, D2D1::ColorF(1, 1, 1, 0)}};
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> collection;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> sweep;
    if (FAILED(target_->CreateGradientStopCollection(stops, 3, collection.GetAddressOf())) ||
        FAILED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(center - band * 0.5f, 0), D2D1::Point2F(center + band * 0.5f, 0)),
                                                  collection.Get(), sweep.GetAddressOf()))) return;
    for (const auto& r : bars) target_->FillRoundedRectangle(r, sweep.Get());
}

Renderer::TextureStats Renderer::textureStats() const {
    TextureStats stats;
    const auto count = [&](const Cover& cover) {
        if (!cover.bitmap) return;
        ++stats.bitmaps; stats.bytes += bitmapBytes(cover.bitmap.Get());
        if (cover.steelbook && cover.steelbook->bitmap) {
            const auto material = cover.steelbook->bitmap->GetPixelSize();
            ++stats.bitmaps; stats.bytes += static_cast<size_t>(material.width) * material.height * 4;
        }
        for (const auto* mask : {cover.steelbook ? cover.steelbook->gloss.Get() : nullptr, cover.steelbook ? cover.steelbook->matte.Get() : nullptr})
            if (mask) { const auto pixels = mask->GetPixelSize(); ++stats.bitmaps; stats.bytes += static_cast<size_t>(pixels.width) * pixels.height; }
        if (cover.steelbook && cover.steelbook->relief) {
            const auto relief = cover.steelbook->relief->GetPixelSize();
            ++stats.bitmaps; stats.bytes += static_cast<size_t>(relief.width) * relief.height * 4;
        }
    };
    for (const auto* list : {&covers_, &logos_, &heroes_, &posters_, &discs_, &plugin_thumbnails_, &glance_thumbnails_, &tarot_images_, &profile_images_})
        for (const auto& cover : *list) count(cover);
    for (const auto& tile : tile_thumbnails_) for (const auto& thumbnail : tile) count(thumbnail);
    return stats;
}

ID2D1BitmapBrush* Renderer::grayBrush(Cover& cover) {
    if (cover.gray_brush || cover.gray_failed || !target_ || !cover.loaded()) return cover.gray_brush.Get();
    // A failure is remembered: this runs while painting, and a broken image would be decoded
    // again on every frame.
    cover.gray_failed = true;
    const auto bytes = cover.encoded(); // Low memory: gone once nobody keeps them; the colour picture stays.
    if (!bytes) return nullptr;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> gray, converter;
    if (FAILED(wic_->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes->data()), static_cast<DWORD>(bytes->size()))) ||
        FAILED(wic_->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return nullptr;
    // Same pixel size as the colour bitmap (decode caps), so the fill transform matches.
    IWICBitmapSource* source = frame.Get();
    UINT width = 0, height = 0;
    if (cover.bitmap && SUCCEEDED(frame->GetSize(&width, &height))) {
        const auto size = cover.bitmap->GetPixelSize();
        if ((size.width != width || size.height != height) &&
            SUCCEEDED(wic_->CreateBitmapScaler(scaler.GetAddressOf())) &&
            SUCCEEDED(scaler->Initialize(frame.Get(), size.width, size.height, WICBitmapInterpolationModeFant))) source = scaler.Get();
    }
    if (FAILED(wic_->CreateFormatConverter(gray.GetAddressOf())) ||
        FAILED(gray->Initialize(source, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(wic_->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(gray.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(target_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, cover.gray_bitmap.GetAddressOf())) ||
        FAILED(target_->CreateBitmapBrush(cover.gray_bitmap.Get(), cover.gray_brush.GetAddressOf()))) {
        cover.gray_bitmap.Reset();
        return nullptr;
    }
    cover.gray_brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    cover.gray_failed = false;
    return cover.gray_brush.Get();
}

void Renderer::updateViewport() {
    if (desktop_viewport_) { viewport_ = desktopViewport(); return; }
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    const float width = static_cast<float>(std::max(1L, rect.right));
    const float height = static_cast<float>(std::max(1L, rect.bottom));
    viewport_.scale = std::max(0.42f, std::min(width / 1600.0f, height / 900.0f));
    viewport_.width = width / viewport_.scale;
    viewport_.height = height / viewport_.scale;
}

// The window's DPI, shrunk only so the layout keeps at least 1100 × 700 DIPs (the TV dialogs that
// open over the library fit then too).
Viewport Renderer::desktopViewport() const {
    RECT rect{};
    if (hwnd_) GetClientRect(hwnd_, &rect);
    const float width = static_cast<float>(std::max(1L, rect.right));
    const float height = static_cast<float>(std::max(1L, rect.bottom));
    const UINT dpi = hwnd_ ? GetDpiForWindow(hwnd_) : 0;
    const float system = dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
    Viewport view;
    view.scale = std::clamp(std::min({system, width / 1100.0f, height / 700.0f}), 0.5f, 4.0f);
    view.width = width / view.scale;
    view.height = height / view.scale;
    return view;
}

// Tryb TV's dialogs over a desktop page: their layouts on a canvas a little larger than the
// window's DIPs, so they open at desktop proportions.
Viewport Renderer::desktopModalViewport() const {
    const auto view = desktopViewport();
    constexpr float compact = 0.86f;
    Viewport modal;
    modal.scale = view.scale * compact;
    modal.width = view.width / compact;
    modal.height = view.height / compact;
    return modal;
}

float Renderer::focusY() const { return carousel::bottom(viewport_.height) - 280.0f; }
int Renderer::distance(size_t index) const { return carousel::distance(index, selected_, cards_->size()); }
void Renderer::brush(D2D1_COLOR_F value) { solid_->SetColor(value); }

void Renderer::fillSquircle(ID2D1Brush* brush) {
    if (tile_shape_ == AppSettings::TileShape::square) { target_->FillRectangle(D2D1::RectF(0, 0, 1, 1), brush); return; }
    if (tile_shape_ == AppSettings::TileShape::rounded) {
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, 1, 1), 0.10f, 0.10f), brush); return;
    }
    const bool compact = squircle_corner_ < full_corner && squircle_compact_;
    const auto& realization = compact ? squircle_compact_fill_ : squircle_fill_;
    if (realization && realizer_) realizer_->DrawGeometryRealization(realization.Get(), brush);
    else target_->FillGeometry(compact ? squircle_compact_.Get() : squircle_.Get(), brush);
}

void Renderer::strokeSquircle(ID2D1Brush* brush, float width) {
    if (tile_shape_ == AppSettings::TileShape::square) { target_->DrawRectangle(D2D1::RectF(0, 0, 1, 1), brush, width); return; }
    if (tile_shape_ == AppSettings::TileShape::rounded) {
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, 1, 1), 0.10f, 0.10f), brush, width); return;
    }
    const bool compact = squircle_corner_ < full_corner && squircle_compact_;
    auto* geometry = compact ? squircle_compact_.Get() : squircle_.Get();
    auto& strokes = compact ? squircle_compact_strokes_ : squircle_strokes_;
    if (realizer_ && width > 0) {
        // Unit-space width in 1.5% steps (log scale): a card animating between two sizes
        // reuses a few dozen realizations instead of making one per frame.
        constexpr float step = 1.015f;
        const auto key = static_cast<uint32_t>(std::lround(std::log(width) / std::log(step)) + 100000);
        auto found = strokes.find(key);
        if (found == strokes.end()) {
            if (strokes.size() >= 256) strokes.clear();
            ComPtr<ID2D1GeometryRealization> stroke;
            const float quantized = std::pow(step, static_cast<float>(static_cast<int>(key) - 100000));
            if (SUCCEEDED(realizer_->CreateStrokedGeometryRealization(geometry, squircle_tolerance,
                                                                     quantized, nullptr, stroke.GetAddressOf())))
                found = strokes.emplace(key, std::move(stroke)).first;
        }
        if (found != strokes.end()) { realizer_->DrawGeometryRealization(found->second.Get(), brush); return; }
    }
    target_->DrawGeometry(geometry, brush, width);
}

ComPtr<ID2D1Geometry> Renderer::tileShape(D2D1_RECT_F box, AppSettings::TileShape shape) const {
    // Rectangular tiles keep circular rounded corners. Squircle keeps the same
    // superellipse as square cards. One geometry clips the image and its outline.
    const float width = box.right - box.left, height = box.bottom - box.top;
    if (!factory_ || width <= 0 || height <= 0) return {};
    if (shape == AppSettings::TileShape::square) {
        ComPtr<ID2D1RectangleGeometry> result;
        factory_->CreateRectangleGeometry(box, result.GetAddressOf());
        return result;
    }
    // corner_reference_ (Metro): corners as on a tile whose shorter side is at most that long, so a
    // large tile is not rounder than the small ones next to it.
    const float shorter = std::min(width, height);
    const float reference = corner_reference_ > 0 ? std::min(shorter, corner_reference_) : shorter;
    if (shape == AppSettings::TileShape::rounded) {
        const float radius = reference * 0.10f;
        ComPtr<ID2D1RoundedRectangleGeometry> result;
        factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(box, radius, radius), result.GetAddressOf());
        return result;
    }
    ComPtr<ID2D1TransformedGeometry> result;
    if (squircle_corner_ >= full_corner && reference >= shorter && std::abs(width - height) < 0.01f) {
        factory_->CreateTransformedGeometry(squircle_.Get(), D2D1::Matrix3x2F::Scale(width, height) *
                                            D2D1::Matrix3x2F::Translation(box.left, box.top), result.GetAddressOf());
        return result;
    }
    if (reference < shorter || squircle_corner_ >= full_corner) {
        // Capped corner: one unit-height shape per aspect and relative corner.
        const auto aspect_key = static_cast<uint64_t>(std::max(1L, std::lround(width / height * 256.0f)));
        const auto corner_key = static_cast<uint64_t>(std::max(1L, std::lround(squircle_corner_ * reference / height * 2048.0f)));
        const uint64_t capped = aspect_key << 20 | corner_key;
        auto found = capped_shapes_.find(capped);
        if (found == capped_shapes_.end()) {
            if (capped_shapes_.size() >= 256) capped_shapes_.clear(); // Tiles easing between sizes pass many.
            auto path = squircleRect(factory_.Get(), static_cast<float>(aspect_key) / 256.0f, 1, static_cast<float>(corner_key) / 2048.0f);
            if (!path) return {};
            found = capped_shapes_.emplace(capped, std::move(path)).first;
        }
        factory_->CreateTransformedGeometry(found->second.Get(), D2D1::Matrix3x2F::Scale(width / (static_cast<float>(aspect_key) / 256.0f), height) *
                                            D2D1::Matrix3x2F::Translation(box.left, box.top), result.GetAddressOf());
        return result;
    }
    // XMB/Flix: corners sized by the shorter side and kept round (not stretched) on posters and
    // capsules. One unit-height shape per aspect, scaled uniformly (within the 1/256 quantisation).
    // Keyed by the aspect and the relative corner: views draw 0.2 and 0.4 corners of the same aspect
    // (posters, capsules, the drawer's icons), and one must never be handed the other's shape.
    const auto aspect_key = static_cast<uint32_t>(std::clamp(std::lround(width / height * 256.0f), 1L, 0xFFFFFL));
    const auto corner_key = static_cast<uint32_t>(std::clamp(std::lround(squircle_corner_ * 1024.0f), 0L, 0x7FFL));
    const uint32_t key = aspect_key << 11 | corner_key;
    auto found = compact_shapes_.find(key);
    if (found == compact_shapes_.end()) {
        if (compact_shapes_.size() >= 128) compact_shapes_.clear(); // The spring outline passes many aspects.
        const float aspect = static_cast<float>(aspect_key) / 256.0f;
        auto path = squircleRect(factory_.Get(), aspect, 1, squircle_corner_ * std::min(aspect, 1.0f));
        if (!path) return {};
        found = compact_shapes_.emplace(key, std::move(path)).first;
    }
    const float unit_width = static_cast<float>(found->first >> 11) / 256.0f;
    factory_->CreateTransformedGeometry(found->second.Get(), D2D1::Matrix3x2F::Scale(width / unit_width, height) *
                                        D2D1::Matrix3x2F::Translation(box.left, box.top), result.GetAddressOf());
    return result;
}
