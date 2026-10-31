#pragma once

#include "../model/game_card.hpp"
#include "../i18n/i18n.hpp"
#include "../artwork/text_crop.hpp"
#include "image_decoder.hpp"
#include "swap_chain.hpp"
#include "view_context.hpp"
#include "controller_art.hpp"
#include "text_layout_cache.hpp"
#include "psfive_layout.hpp"
#include "quick_menu.hpp"
#include "palette.hpp"
#include "avatar.hpp"
#include "../platform/user_config.hpp"
#include "../platform/performance.hpp"
#include "../platform/layered_surface.hpp"

#include <Windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1_2.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef LAUNCHME_UI_TEST
#include <functional>
#endif

struct Viewport {
    float scale = 1;
    float width = 1600;
    float height = 900;
};

class Renderer {
public:
    bool initialize(HWND hwnd, bool layered = false);
    bool paintOverlay(const ViewContext& context);
    void setDrawerStyle(DrawerStyle style) { drawer_style_ = style; }
    void setFontFamily(std::wstring family);
    void setCoverAccent(uint32_t rgb) { cover_accent_ = rgb; }
    void setUiAccent(uint32_t rgb) { ui_accent_ = rgb ? rgb : AppSettings{}.ui_accent_rgb; }
    // Settings → Interfejs → Kolor przycisków: the focused button's fill (0 = the interface accent)
    // and the tint of the others; captions take dark or light ink, whichever contrasts more.
    void setButtonTheme(uint32_t focus_rgb, AppSettings::ButtonIdle idle) { button_rgb_ = focus_rgb; button_idle_ = idle; }
    // Settings → Interfejs → Zaznaczona okładka: an empty band between the focus outline and the picture.
    void setFocusGap(AppSettings::FocusGap gap) { focus_gap_ = gap; }
    // Settings → Interfejs: halo strength (0..2) and mutually exclusive outline animations.
    void setGlow(float level, bool pulse, bool trace = false) {
        glow_level_ = std::clamp(level, 0.0f, 2.0f); glow_trace_ = trace; glow_pulse_ = pulse && !trace;
    }
    // Settings → Interfejs → Siatka na tle: strength of the dot grid over background video (0..1).
    void setBackgroundGrid(float level, AppSettings::GridStyle style = AppSettings::GridStyle::dots) {
        background_grid_ = std::clamp(level, 0.0f, 1.0f); background_grid_style_ = style;
    }
    void setTileShape(AppSettings::TileShape shape) { tile_shape_ = shape; }
    void setPanelBorders(bool enabled) { panel_borders_ = enabled; }
    // Settings → Interfejs → Efekt szkła: 0 = solid panels, 1 = the strongest frosted glass.
    void setGlass(float level) { glass_level_ = std::clamp(level, 0.0f, 1.0f); }
    void setConsoleColors(bool enabled) { console_colors_ = enabled; }
    void resize();
    // Desktop mode (src/ui/desktop_*renderer.cpp): its library is laid out in DIPs at the window's DPI
    // (a responsive layout) instead of on the 1600 × 900 TV canvas. desktopViewport() is that
    // viewport whatever the current screen (hit tests between two frames).
    void setDesktopViewport(bool desktop) { if (desktop_viewport_ != desktop) { desktop_viewport_ = desktop; updateViewport(); } }
    Viewport desktopViewport() const;
    // As drawn last: the pane's content height and its collection chips, "+" chip, rating link and
    // achievements number (empty when the game has none: nothing to open).
    float desktopPaneHeight() const { return desktop_pane_height_; }
    const std::vector<settings_ui::Rect>& desktopChips() const { return desktop_chips_; }
    settings_ui::Rect desktopChipAdd() const { return desktop_chip_add_; }
    settings_ui::Rect desktopRating() const { return desktop_rating_; }
    settings_ui::Rect desktopProtonDb() const { return desktop_protondb_; }
    // The pane's Developer and Wydawca values: a click shows that studio's games.
    const std::array<settings_ui::Rect, 2>& desktopStudios() const { return desktop_studios_; }
    settings_ui::Rect desktopAchievements() const { return desktop_achievements_; }
    settings_ui::Rect desktopCloud() const { return desktop_cloud_; } // The pane's cloud saves (Aurelia), as last drawn.
    // Desktop dialogs measured while drawn: their controls (topmost last) and content height.
    const std::vector<desktop_ui::SheetHotspot>& desktopHotspots() const { return desktop_hotspots_; }
    // Tryb TV's dialogs drawn as desktop dialogs in the last frame (Kolekcje, Dodaj grę, Zidentyfikuj
    // grę): which one, and its controls in desktop DIPs (desktop_dialogs_renderer.cpp).
    desktop_ui::DialogKind desktopModal() const { return desktop_modal_; }
    // Tryb TV's Urządzenia, kadrowanie and plugin pages inside a desktop sheet: their canvas, and the
    // sheet body's top-left corner in client pixels (the launcher maps the pointer through it).
    Viewport desktopEmbedViewport() const;
    D2D1_POINT_2F desktopEmbedOrigin() const;
    settings_ui::Rect desktopEmbedArea() const;
    const std::vector<desktop_ui::SheetHotspot>& desktopModalHotspots() const { return desktop_modal_hotspots_; }
    float desktopSheetHeight() const { return desktop_sheet_height_; }
    void armDesktopBackdrop() { desktop_backdrop_armed_ = true; } // The next library frame is kept.
    // The caret index nearest to x (DIPs from where the text starts) in a desktop field's text.
    size_t desktopCaretAt(const std::wstring& value, float x, bool title_font);
    void resetCovers(size_t count, bool preserve_background = true);
    // Keeps the first `keep` cards' bitmaps (XMB top row) and clears the rest, so a column
    // change does not decode the whole row again.
    void keepCovers(size_t keep, size_t count);
    // A rebuilt card list over the same screen: slot i takes over old slot from[i] (out of range =
    // empty), so unchanged tiles keep their decoded art instead of fading in one by one again.
    void remapCovers(const std::vector<size_t>& from, bool preserve_background = true);
    // Changes with every reset/keep/remap: equal values mean the slots still belong to the same list.
    uint64_t slotGeneration() const { return slot_generation_; }
    // Artwork setters share the caller's encoded buffer (ImageBytes); the vector overloads
    // take ownership of a buffer nobody else keeps.
    // 2x2 preview inside an XMB collection tile.
    void setTileThumbnail(size_t index, int slot, ImageBytes pixels, CropFocus focus);
    void setTileThumbnail(size_t index, int slot, std::vector<uint8_t> pixels, CropFocus focus) { setTileThumbnail(index, slot, shareImage(std::move(pixels)), focus); }
    bool hasTileThumbnail(size_t index, int slot) const {
        return index < tile_thumbnails_.size() && slot >= 0 && slot < 4 && tile_thumbnails_[index][slot].loaded();
    }
    // `short_edge`: the decode cap (0: the default for covers and posters, 1024 px). The desktop
    // grid asks for its cell size; the same bytes asked for at another size decode again while
    // the shown bitmap stays until the new one is uploaded.
    void setCover(size_t index, ImageBytes pixels, CropFocus focus, uint32_t short_edge = 0);
    void setCover(size_t index, std::vector<uint8_t> pixels, CropFocus focus) { setCover(index, shareImage(std::move(pixels)), focus); }
    // Desktop grid: decodes and uploads of this card's poster/cover come in `rank` order (lower
    // first; 0 is every other image); see DecodeTicket::priority.
    void rankArt(size_t index, int rank);
    // A card's poster or cover bitmap is on screen-ready (uploaded), and the pixel bytes of both.
    bool artShown(size_t index) const {
        return (index < posters_.size() && posters_[index].bitmap) || (index < covers_.size() && covers_[index].bitmap);
    }
    size_t artBytes(size_t index) const;
    void setCoverFocus(size_t index, CropFocus focus) { if (index < covers_.size()) covers_[index].focus = focus; }
    // System → Wydajność → Tryb niskiego zużycia RAM (AppSettings::low_memory): card art is decoded at
    // the size it is drawn (and again larger when drawn larger), and covers, posters, logos and discs let
    // their encoded bytes go once uploaded (the launcher's cache or the disk has them). block_compress
    // (AppSettings::texture_compression): opaque art goes to the GPU as BC1 blocks and logos as BC3 where
    // the device samples them; backgrounds only in the low memory mode (smooth gradients over the whole
    // screen show BC1's steps most). From the next decode on.
    void setLowMemory(bool enabled, bool block_compress = false) { low_memory_ = enabled; block_compress_ = block_compress; limitTextureMemory(); }
    bool lowMemory() const { return low_memory_; }
    // Compressed art kept on disk in this folder (texture_cache.hpp; empty: none).
    void setTextureCache(const std::filesystem::path& folder) { decoder_.useTextureCache(folder, texture_cache_budget); }
    // Behind a game (src/app/memory.cpp): Direct2D's caches and the driver's temporary allocations go
    // back; the bitmaps stay (the low memory mode releases those too, releaseGraphics).
    void trimGraphics();
    // Game videos straight on the GPU (game_video.cpp, Windows): the window's Direct3D 11 device
    // (null without the swap chain target, or with LAUNCHME_VIDEO_GPU=0), a BGRA texture of this size
    // the next frame is drawn into, and that frame shown once it is there. Linux copies frames
    // (setVideoFrame). deviceGeneration() changes with every new device.
    void* videoDevice();
    void* videoTexture(uint32_t width, uint32_t height);
    void showVideoTexture();
    uint64_t deviceGeneration() const { return device_generation_; }
    // The launcher waits behind a game: every device resource goes (bitmaps, brushes, the Direct2D
    // target and with it its GPU device), and so do the card art slots and cached text layouts. The
    // next paint makes the target again; takeArtLost() then asks the launcher to bind the art again.
    void releaseGraphics();
    // Card art slots emptied since the last call (releaseGraphics, or a slot whose bytes were let go
    // and whose bitmap went with the device): the launcher binds them again from its cache or disk.
    bool takeArtLost() { const bool lost = art_lost_; art_lost_ = false; return lost; }
    // A card's background picture given back (low memory: only the focused card's neighbours keep one).
    void releaseHero(size_t index) { if (index < heroes_.size()) heroes_[index] = {}; }
    // A card's cover, poster or disc still holds its encoded bytes (waiting for its decode or upload).
    bool artPending(size_t index) const;
    // True once bytes were handed over; the bitmap itself may still be decoding.
    bool hasCover(size_t index) const { return index < covers_.size() && covers_[index].loaded(); }
    // Drops a card's cover bitmap and bytes (a grid card scrolled far away); set again to show it.
    void releaseCover(size_t index) { if (index < covers_.size()) covers_[index] = {}; }
    void releasePreview(size_t index) { if (index < heroes_.size()) heroes_[index] = {}; if (index < logos_.size()) logos_[index] = {}; }
    // Flix artwork per card (src/ui/flix_renderer.cpp): the vertical poster, or in the first
    // row the landscape capsule (a background image stands in until it arrives).
    void setPoster(size_t index, ImageBytes pixels, uint32_t short_edge = 0);
    bool hasPoster(size_t index) const { return index < posters_.size() && posters_[index].loaded(); }
    void releasePoster(size_t index) { if (index < posters_.size()) posters_[index] = {}; }
    void setDisc(size_t index, ImageBytes pixels);
    bool hasDisc(size_t index) const { return index < discs_.size() && discs_[index].loaded(); }
    void releaseDisc(size_t index) { if (index < discs_.size()) discs_[index] = {}; }
    // Przegląd: the focused game's vertical poster in the panel; empty bytes clear it.
    void setPlatformCover(size_t index, ImageBytes pixels);
    void setPlatformCover(size_t index, std::vector<uint8_t> pixels) { setPlatformCover(index, shareImage(std::move(pixels))); }
    void setLogo(size_t index, ImageBytes pixels);
    void setLogo(size_t index, std::vector<uint8_t> pixels) { setLogo(index, shareImage(std::move(pixels))); }
    // focus: the part of the background a screen of another shape shows (background_focus.hpp).
    void setHero(size_t index, ImageBytes pixels, HeroTone tone = HeroTone::full, bool force = false, CropFocus focus = {});
    void setHero(size_t index, std::vector<uint8_t> pixels, HeroTone tone = HeroTone::full, bool force = false, CropFocus focus = {}) {
        setHero(index, shareImage(std::move(pixels)), tone, force, focus);
    }
    // The crop editor moves the focus of the background shown (no new decode).
    void setHeroFocus(size_t index, CropFocus focus);
    // Game video over the focused game's background (src/app/game_video.cpp): BGRA frames,
    // stride width * 4. It fades in over the picture; clearVideo removes it at once.
    void setVideoFrame(const uint8_t* bgra, uint32_t width, uint32_t height);
    void clearVideo();
    bool videoShown() const { return video_bitmap_ != nullptr; }
    bool marqueeActive() const { return marquee_active_; } // The last frame had a gliding name (fadingLine): paint the next one too.
    float videoFade() const; // 0..1: the playing video's 600 ms fade-in (0 without one).
    // The playing video cover-fitted into `rect` (clipped to clip when given), faded in; false when none shows.
    bool drawVideoIn(D2D1_RECT_F rect, ID2D1Geometry* clip, float opacity);
    void setFriendAvatar(uint64_t id, std::vector<uint8_t> pixels);
    void clearFriendAvatars();
    void resetPluginThumbnails(size_t count) { plugin_thumbnails_.clear(); plugin_thumbnails_.resize(count); }
    void setPluginThumbnail(size_t index, ImageBytes pixels, CropFocus focus);
    void resetGlanceThumbnails(size_t count) { glance_thumbnails_.clear(); glance_thumbnails_.resize(count); }
    void setGlanceThumbnail(size_t index, ImageBytes pixels);
    void measureGlance(glance_ui::State& state);
    // Tryb TV's rows with tags (Pluginy): Row::lines from the description's wrapped length at the
    // row's text width, so the row grows to hold it above its tags. The desktop window and thin
    // rows keep the default (their descriptions end with an ellipsis).
    void fitSettingsDescriptions(std::vector<settings_ui::Row>& rows, const settings_ui::State& state, bool desktop);
    // Tarot's result cards (src/ui/tarot_renderer.cpp): a poster each, or a cover until it comes.
    void resetTarotImages(size_t count) { tarot_images_.clear(); tarot_images_.resize(count); tarot_posters_.assign(count, false); }
    void setTarotImage(size_t index, ImageBytes pixels, bool poster);
    bool tarotPoster(size_t index) const { return index < tarot_posters_.size() && tarot_posters_[index]; }
    bool tarotImage(size_t index) const { return index < tarot_images_.size() && tarot_images_[index].loaded(); }
    // Profil's pictures (src/ui/profile_renderer.cpp): the player's, game covers, achievement icons.
    void resetProfileImages(size_t count) { profile_images_.clear(); profile_images_.resize(count); }
    void setProfileImage(size_t index, ImageBytes pixels);
    bool profileImage(size_t index) const { return index < profile_images_.size() && profile_images_[index].loaded(); }
    void setPlatformBadge(const std::wstring& platform, std::vector<uint8_t> pixels);
    void resetArtworkThumbnails(size_t count);
    // The file picker's image preview (src/ui/file_browser_renderer.cpp); empty bytes clear it.
    void setFileBrowserPreview(std::vector<uint8_t> bytes);
    // Dodaj / Edytuj grę: its poster still decoding (the launcher paints until it shows).
    bool customPosterPending() const { return custom_poster_.loaded() && !custom_poster_.bitmap; }
    void setArtworkThumbnail(size_t index, std::vector<uint8_t> bytes, bool logo);
    // Screensaver (src/ui/screensaver_renderer.cpp): three image slots, decoded off the UI thread.
    static constexpr int screensaver_slots = 3;
    enum class SlotState { empty, pending, ready, failed };
    void setScreensaverImage(int slot, std::vector<uint8_t> bytes, bool hero);
    void clearScreensaver();
    SlotState screensaverSlot(int slot) const;
    // True when the last game box was drawn in perspective (ID2D1DeviceContext), false for the flat fallback.
    bool perspectiveBoxes() const { return perspective_boxes_; }
    void paint(const std::vector<GameCard>& cards, size_t selected, bool scanning,
               float elapsed, float detail, const ViewContext& context);
    Viewport viewport() const { return viewport_; }
    // The achievements row of the last painted details screen (empty without one), for hit tests.
    settings_ui::Rect detailsAchievements() const { return details_achievements_; }
    settings_ui::Rect detailsComparison() const { return details_comparison_; }
    settings_ui::Rect ratingLink() const { return rating_link_; }
    // Towarzyszka: her home bubble as last painted (clicks open the chat) and how far her chat scrolls.
    settings_ui::Rect companionBubble() const { return companion_bubble_; }
    float companionScrollLimit() const { return companion_scroll_limit_; }
    // Her photo in the chat's log as last painted (its visible part; empty when none is in view): a click enlarges it.
    settings_ui::Rect companionPhoto() const { return companion_photo_rect_; }
    // The visible glyph rectangles from the last frame, including wrapped titles.
    std::wstring companionLinkAt(float x, float y) const {
        for (const auto& link : companion_links_) if (link.rect.contains(x, y)) return link.title;
        return {};
    }
    std::wstring companionVideoAt(float x, float y) const {
        for (const auto& link : companion_videos_) if (link.rect.contains(x, y)) return link.title;
        return {};
    }
    // The Metro tab under a point of the last painted home screen; -1: none.
    int metroTabAt(float x, float y) const {
        for (size_t i = 0; i < metro_tab_rects_.size(); ++i) if (metro_tab_rects_[i].contains(x, y)) return static_cast<int>(i);
        return -1;
    }
    // The PSfive tab under a point of the last painted home screen; -1: none.
    int psfiveTabAt(float x, float y) const {
        for (size_t i = 0; i < psfive_tab_rects_.size(); ++i) if (psfive_tab_rects_[i].contains(x, y)) return static_cast<int>(i);
        return -1;
    }
    psfive_layout::MoreMenu psfiveMoreMenu(size_t categories) const {
        const float left = psfive_tab_rects_.size() > psfive_layout::visible_tabs ?
            psfive_tab_rects_[psfive_layout::visible_tabs].left : psfive_layout::tabs_left;
        return {viewport_.width, viewport_.height, categories, left};
    }
    // Images are still decoding or fading in; the frame loop should keep painting.
    bool animating() const;
    // Text still on its way (a game's description or facts): faint bars, a soft light sweeping
    // them left to right while they wait (keeps animating() true while drawn).
    void drawSkeleton(D2D1_RECT_F area, int lines, float step, float alpha = 1);
    // Uploaded card artwork (covers, logos, heroes, XMB thumbnails): bitmaps and their pixel bytes.
    struct TextureStats { size_t bitmaps = 0, bytes = 0; };
    TextureStats textureStats() const;
    void profile(bool enabled) { profiling_ = enabled; }
    RenderTiming timing() const { return timing_; }
#ifdef LAUNCHME_UI_TEST
    D2D1_COLOR_F buttonInkForTest(float focus) const { return buttonInk(focus); }
    int overlayAlphaForTest(int x, int y) const;
    bool exitSnapshotForTest() const { return exit_frame_ != nullptr; }
    bool screenSnapshotForTest() const { return screen_frame_ != nullptr || screen_ui_target_ != nullptr; }
    bool screenBowTargetForTest() const { return screen_bow_target_ != nullptr; }
    bool captureForTest(const std::filesystem::path& file, const std::function<void()>& draw);
    uint32_t uiAccentForTest() const { return ui_accent_; }
    float glowLevelForTest() const { return glow_level_; }
    float backgroundGridForTest() const { return background_grid_; }
    AppSettings::GridStyle backgroundGridStyleForTest() const { return grid_mask_style_; }
    int backgroundGridCellForTest() const { return grid_mask_ ? grid_mask_cell_ : 0; }
    bool glowPulseForTest() const { return glow_pulse_; }
    bool glowTraceForTest() const { return glow_trace_; }
    bool fileBrowserPreviewForTest() const { return file_browser_preview_.loaded(); }
    bool flat_boxes_for_test = false; // Draw game boxes through the fallback even when perspective works.
    bool glance_frosted_for_test = false;
    bool steelbook_for_test = true; // Comparison captures of the original print and its subtle finish.
    bool posterReadyForTest(size_t index) const { return index < posters_.size() && posters_[index].bitmap; }
    bool coverReadyForTest(size_t index) const { return index < covers_.size() && covers_[index].bitmap; }
    // Low memory (low_memory_ui_cases.hpp).
    ID2D1Bitmap* coverBitmapForTest(size_t index) const { return index < covers_.size() ? covers_[index].bitmap.Get() : nullptr; }
    bool coverBytesDroppedForTest(size_t index) const { return index < covers_.size() && covers_[index].dropped && !covers_[index].pixels; }
    bool coverCompressedForTest(size_t index) const {
        return index < covers_.size() && covers_[index].bitmap && covers_[index].bitmap->GetPixelFormat().format == DXGI_FORMAT_BC1_UNORM;
    }
    bool bc1ForTest() const { return bc1_supported_; }
#ifdef _WIN32
    bool swapChainForTest() const { return static_cast<bool>(swap_); }
#endif
    bool targetForTest() const { return target_ != nullptr; }
    void discardTargetForTest() { discardTarget(); } // As on device loss.
    uint32_t coverEdgeForTest() const { return coverEdge(); }
    void wantEdgeForTest(size_t index, float magnify) { if (index < covers_.size()) wantEdge(covers_[index], magnify); }
    bool steelbookReadyForTest(size_t index) const {
        return index < posters_.size() && posters_[index].steelbook && posters_[index].steelbook->brush;
    }
    bool steelbookShaderForTest(size_t index) const {
        return index < posters_.size() && posters_[index].steelbook && posters_[index].steelbook->masked && posters_[index].steelbook->printed;
    }
    bool steelbookGlossForTest(size_t index) const {
        return index < posters_.size() && posters_[index].steelbook && posters_[index].steelbook->gloss;
    }
    bool steelbookReliefForTest(size_t index) const {
        return index < posters_.size() && posters_[index].steelbook && posters_[index].steelbook->rim_light && posters_[index].steelbook->rim_shade;
    }
    // Every drawn controller family in a grid (inside captureForTest); true when all geometry was built.
    bool paintControllerGalleryForTest();
    // Benchmark only: decode on the worker threads like the app instead of inline.
    void startDecoderForTest() { decoder_.start(hwnd_); }
    const ImageDecoder& decoderForTest() const { return decoder_; }
    // Background layers: visible level of the shown picture and whether another one fades out.
    float heroLevelForTest() const { return hero_shown_.pixels ? hero_shown_.level : 0; }
    bool heroShownForTest() const { return hero_shown_.bitmap != nullptr; }
    void settleVideoForTest() { video_started_ -= std::chrono::seconds(2); } // Past its fade-in.
    bool heroLeavingForTest() const { return hero_leaving_.pixels != nullptr; }
    ID2D1Bitmap* heroBitmapForTest(size_t index) const { return index < heroes_.size() ? heroes_[index].bitmap.Get() : nullptr; }
    ID2D1Bitmap* heroShownBitmapForTest() const { return hero_shown_.bitmap.Get(); }
    ID2D1Bitmap* heroLeavingBitmapForTest() const { return hero_leaving_.bitmap.Get(); }
    D2D1_COLOR_F platformTileFillForTest(const Game& game) const { return platformTileFill(game, 1); }
    HRESULT draw_result_for_test = S_OK;
    float psfiveClockAdvanceForTest(const std::wstring& value) { return psfive_clock_font_ ? textAdvance(value, psfive_clock_font_.Get()) : 0; }
#endif

private:
    struct SteelbookMaterial {
        ImageBytes source;
        std::shared_ptr<DecodeTicket> decoding;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        Microsoft::WRL::ComPtr<ID2D1BitmapBrush> brush;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> gloss; // A8 polished dark ink; null when the art has none.
        Microsoft::WRL::ComPtr<ID2D1Bitmap> matte; // A8 grainy matte print (neither foil nor lacquer).
        Microsoft::WRL::ComPtr<ID2D1Bitmap> relief; // Embossed high-contrast edges; null without strong edges/effects.
        Microsoft::WRL::ComPtr<ID2D1Effect> rim_light, rim_shade; // Relief . light direction -> white/black rims.
        Microsoft::WRL::ComPtr<ID2D1Effect> lighting, highlight, masked;
        Microsoft::WRL::ComPtr<ID2D1Effect> surface_transform, printed;
        std::chrono::steady_clock::time_point revealed{};
        bool failed = false;
    };
    struct Cover {
        ImageBytes pixels; // Encoded bytes; kept to rebuild the bitmap after device loss (low memory: see dropped).
        std::shared_ptr<DecodeTicket> decoding; // In flight on the decoder workers.
        std::chrono::steady_clock::time_point revealed{}; // Async arrival; drives a short fade-in.
        // Low memory: `pixels` let go once uploaded (droppable slots only). `source` still finds them
        // while the launcher's cache keeps them; size and hash recognise the same picture read again.
        bool droppable = false, dropped = false;
        std::weak_ptr<const std::vector<uint8_t>> source;
        size_t source_size = 0;
        uint64_t source_hash = 0;
        bool loaded() const { return (pixels && !pixels->empty()) || dropped; }
        ImageBytes encoded() const { return pixels ? pixels : source.lock(); } // Null once nobody keeps them.
        // `other` is the picture bound here (the same buffer, or the same bytes read again).
        bool same(const ImageBytes& other) const {
            if (!other || other->empty()) return false;
            if (other == pixels) return true;
            if (!dropped) return false;
            if (source.lock() == other) return true;
            return other->size() == source_size && imageHash(*other) == source_hash;
        }
        CropFocus focus;
        uint32_t short_edge = 0; // Decode cap (DecodeKind::short_edge); 0 = full size.
        bool loose_edge = false; // DecodeKind::loose_edge: the desktop grid's cell size.
        bool hero = false;
        HeroTone tone = HeroTone::full;
        bool logo = false;
        bool has_ink_bounds = false;
        D2D1_RECT_F ink_bounds{};
        D2D1_COLOR_F accent{0.94f, 0.96f, 0.97f, 1.0f};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        Microsoft::WRL::ComPtr<ID2D1BitmapBrush> brush;
        // Grayscale variant for a suspended game, decoded on first use only.
        Microsoft::WRL::ComPtr<ID2D1Bitmap> gray_bitmap;
        Microsoft::WRL::ComPtr<ID2D1BitmapBrush> gray_brush;
        bool gray_failed = false; // These pixels could not be made grey on this target: no retry per frame.
        std::shared_ptr<SteelbookMaterial> steelbook; // Allocated lazily for visible Coverflow posters.
        // Półka: a tiny copy of `bitmap` (drawn enlarged it is a soft blur), and the bitmap it was made from.
        Microsoft::WRL::ComPtr<ID2D1Bitmap> blurred, blurred_source;
        // Półka: `bitmap` without colour (made on the GPU on first use), and the bitmap it was made from.
        Microsoft::WRL::ComPtr<ID2D1Bitmap> desaturated, desaturated_source;
    };

    // Swap chain frames (swap_chain.hpp): waited for before drawing, shown after EndDraw; a lost
    // device turns into D2DERR_RECREATE_TARGET as an HWND target's EndDraw reports it.
    void beginFrame();
    HRESULT finishFrame(HRESULT drawn);
    // The target's Direct2D device (Windows; Skia manages its own caches on Linux): its texture cache
    // bounded (low memory: less), and caches unused for a while let go when the screen changes (at
    // most every 10 s).
#ifdef _WIN32
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice() const;
#endif
    void limitTextureMemory();
    void releaseIdleCaches();
    static constexpr uint64_t texture_cache_budget = uint64_t{512} << 20; // Compressed art on disk.
    bool createShape();
    bool font(Microsoft::WRL::ComPtr<IDWriteTextFormat>& destination, float size,
              DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);
    bool createFonts();
    void loadLocalFonts();
    bool ensureFactory();
    bool createTarget();
    bool createTargetResources();
    void discardTarget();
    void createBitmap(Cover& cover);
    void redecode(Cover& cover, uint32_t short_edge);
    // Low memory (renderer_resources.cpp): the decode request for a slot; a new picture bound to a
    // droppable slot; the same picture bound again; bytes let go after an upload.
    DecodeKind decodeKind(const Cover& cover) const;
    void bindPicture(Cover& cover, ImageBytes pixels);
    void rebindPicture(Cover& cover, const ImageBytes& pixels);
    void dropBytes(Cover& cover);
    // Default decode caps: 1024 px covers and 256 px thumbnails; in low memory the size a card is
    // drawn at on this screen (and wantEdge raises it where one is drawn larger).
    uint32_t coverEdge() const;
    uint32_t thumbnailEdge() const;
    // Low memory: `magnify` device pixels per bitmap pixel at this draw; well above 1, the picture is
    // decoded again at the size drawn (its shown bitmap stays meanwhile).
    void wantEdge(Cover& cover, float magnify);
    void upload(Cover& cover, bool reveal);
    void uploadDecoded();
    float reveal(const Cover& cover) const;
    ID2D1BitmapBrush* grayBrush(Cover& cover);
    void updateViewport();
    void brush(D2D1_COLOR_F color);
    void line(float x1, float y1, float x2, float y2, D2D1_COLOR_F ink, float width = 1);
    void text(const std::wstring& value, float x, float y, float width, float height,
              IDWriteTextFormat* format, D2D1_COLOR_F ink);
    float textAdvance(const std::wstring& value, IDWriteTextFormat* format); // One line's width with trailing spaces.
    // Field text with a blinking "│" at `caret` (drawn on its own, so the text does not shift while it blinks).
    void fieldText(const std::wstring& value, size_t caret, bool show_caret, float x, float y, float width, float height,
                   IDWriteTextFormat* format, D2D1_COLOR_F ink);
    // Typography (renderer_text.cpp). Positions come from the font's own line box, so every
    // family (SST, Segoe UI, Consolas, Arial) keeps the same rhythm.
    struct TypeMetrics {
        float line = 0, baseline = 0, cap = 0; // Line box height, baseline and capital height from its top.
        float capTop() const { return baseline - cap; }
    };
    // Line box gap between a title and the description directly beneath it; a screen
    // heading (settings_heading_font_) takes heading_group_gap.
    static constexpr float text_group_gap = 5, heading_group_gap = 8;
    TypeMetrics typeMetrics(IDWriteTextFormat* format); // Cached until the fonts are rebuilt.
    // One line whose capitals are centred on `center_y` (a row's or control's centre line).
    void textLine(const std::wstring& value, float x, float center_y, float width, IDWriteTextFormat* format,
                  D2D1_COLOR_F ink, std::optional<DWRITE_TEXT_ALIGNMENT> alignment = {});
    // textLine for a name that may not fit: instead of an ellipsis it fades out on the right and, while
    // moving (the focused row), glides to its end and back with a pause at each end (marqueeActive).
    void fadingLine(const std::wstring& value, float x, float center_y, float width, IDWriteTextFormat* format, D2D1_COLOR_F ink, bool moving);
    // A title (one line, ellipsis) with an optional description directly beneath it, wrapped to
    // `max_lines` and then cut with an ellipsis. The group from the title's capitals to the last
    // description baseline is centred on `y`, or starts at `y` with `top_aligned`.
    // Returns the bottom of the last line box.
    float textGroup(const std::wstring& title, const std::wstring& description, float x, float y, float width,
                    IDWriteTextFormat* title_font, D2D1_COLOR_F title_ink, IDWriteTextFormat* body_font,
                    D2D1_COLOR_F body_ink, int max_lines = 2, bool top_aligned = false, float gap = text_group_gap);
    void drawBackground();
    void drawWallpaper(ID2D1Bitmap* image, float opacity, float offset = 0, CropFocus focus = {});
    // Two wallpapers mixed as one picture (a cross-fade): their opacities add up instead of the
    // upper one covering the lower, so half-way the screen never dims through to the background.
    struct Wallpaper { ID2D1Bitmap* image = nullptr; float opacity = 0; CropFocus focus{}; };
    void drawWallpapers(const Wallpaper& under, const Wallpaper& over);
    // The focused game's background (src/ui/background_renderer.cpp): a new picture waits until
    // the focus has stayed on it for hero_settle_detail and its bitmap is ready, then cross-fades
    // in over hero_fade_seconds. A cross-fade under way always finishes (or runs backwards to the
    // picture it left) before the next one starts: quick steps never leave two pictures half-shown.
    void stepHeroFade(const Cover* slot, const ImageBytes& picture);
    // Kinowy's wallpaper: the same rule, on the bitmaps it shows. Returns the cross-fade's level.
    float stepCinematicFade(ID2D1Bitmap* candidate);
    static constexpr float hero_fade_seconds = 0.3f;
    static constexpr float hero_settle_detail = 0.34f;  // About 0.2 s of detail_ (1.7 per second).
    static constexpr float cinematic_fade_seconds = 0.35f;
    void drawBackgroundGrid(float art);
    void drawLaunchAnimation();
    void drawLibraryBackdrop(float opacity);
    void drawHero();
    void drawCard(const GameCard& card, size_t index);
    static constexpr D2D1_COLOR_F perfect_gold{0.98f, 0.77f, 0.30f, 1.0f}; // Every achievement unlocked.
    bool perfectGame(const Game& game) const;
    // The soft squircle shadow under a square card (larger under the focused one).
    void drawSquareShadow(float x, float y, float size, bool selected, float transparency);
    // Experimental spring focus (ViewContext::focus_ring): the outline on its own rectangle, zoomed
    // around its centre like the focused tile, with the inner glow of a hit list edge.
    void drawFocusRing(D2D1_COLOR_F tint, float transparency, float zoom, bool middle_glow);
    void drawCoverOutline(ID2D1Geometry* shape, D2D1_COLOR_F tint, float transparency, bool middle_glow);
    void drawOrbitOutline(ID2D1Geometry* shape, D2D1_COLOR_F tint, float alpha, float width = 3.2f);
    // The selected tile shape in unit space (under the card transform). Squircle geometry realizations
    // (ID2D1DeviceContext1, Windows 8.1+) it is tessellated once and strokes once per width;
    // otherwise D2D tessellates the 96-segment path on every call.
    void fillSquircle(ID2D1Brush* brush);
    void strokeSquircle(ID2D1Brush* brush, float width);
    Microsoft::WRL::ComPtr<ID2D1Geometry> tileShape(D2D1_RECT_F box, AppSettings::TileShape shape) const;
    SessionState sessionOf(const Game& game) const;
    // Fill under a store or console logo tile: consoles on their platform colour (or dark with
    // console_colors_ off), stores always dark. Shared by drawCard and drawRectangularCard.
    D2D1_COLOR_F platformTileFill(const Game& game, float alpha) const;
    // Status gradient is painted beneath outlines; the badge, text and progress above it.
    void drawCardStatusShade(float x, float y, float width, float height, float transparency, ID2D1Geometry* shape = nullptr);
    void drawCardBadge(float x, float y, float width, float height, float transparency, bool frozen);
    void drawCardStatus(float x, float y, float width, float height, float transparency, const std::wstring& label,
                        std::optional<float> fraction = {}, D2D1_COLOR_F bar = {});
    // An installation in the place of a button's caption: its phase and percent above a thin track,
    // filled to the share (a sweeping segment while the total is unknown). True when drawn: the game
    // installs and is not running; otherwise the caller draws its caption.
    bool drawInstallProgress(const Game& game, D2D1_RECT_F button, D2D1_COLOR_F ink, D2D1_COLOR_F track, IDWriteTextFormat* font);
    // Cover-fit around the crop focus, or contain the entire image without cropping.
    // `realized`: `clip` already tessellated (ID2D1DeviceContext1); filled through it when given.
    void fillImage(Cover& image, D2D1_RECT_F box, float radius, float opacity, bool gray = false, ID2D1Geometry* clip = nullptr, bool contain = false,
                   ID2D1GeometryRealization* realized = nullptr);
    // A poster in a poster's frame: one of nearly its shape fills it (a sliver cut, no bands at the sides);
    // another shape (a square cover standing in) shows whole over a dimmed fill of itself.
    void fillPoster(Cover& image, D2D1_RECT_F box, float radius, float opacity, bool gray, ID2D1Geometry* clip);
    void drawExitPrompt();
    void beginScreenForeground(); // Wallpaper/shading stay on the window; animate only the UI above them.
    HRESULT endScreenForeground();
    void drawScreenTransition(float progress, ID2D1Bitmap* ui_frame = nullptr); // Screen entrance: fade-in, zoom and subtle 4-edge lens bow.
    void drawExitTransition(float progress); // Frozen frame, subtle bow and one descending light.
    void drawPicker(const std::wstring& title, const std::vector<std::wstring>& labels, int selected);
    void drawPowerMenu(); // Zasilanie in Tryb TV (power_menu::Geometry).
    void drawEmptyState();
    void drawFooter();
    void drawPlatformBadge();
    // The platform's logo (as the corner badge draws it) at most `height` tall, left-aligned at x and
    // centred on center_y; its name in `fallback` while it has no logo. Returns the width used.
    bool hasPlatformMark(const std::wstring& platform) const;
    float platformMarkWidth(const std::wstring& platform, float height, float max_width) const;
    float drawPlatformMark(const std::wstring& platform, float x, float center_y, float height, float max_width, float alpha,
                           IDWriteTextFormat* fallback);
    void drawContextMenu();
    void drawNotification();
    void drawVolumeIndicator();
    void drawAudioIcon(float x, float y, bool input, bool muted, D2D1_COLOR_F ink);
    void drawControllerIcon(float x, float y, float size, D2D1_COLOR_F ink);
    // Drawn controller picture (src/ui/controller_art.cpp), centred in `box` at a shared scale.
    void drawControllerArt(devices::Picture picture, D2D1_RECT_F box);
    using ControllerArt = std::array<Microsoft::WRL::ComPtr<ID2D1PathGeometry>, controller_art::layer_count>;
    const ControllerArt* controllerArt(devices::Picture picture);
    void drawFriends();
    void drawFriendsList(float x);
    void drawFriendChat(float x); // A friend's conversation in the drawer (friend_chat_view.hpp).
    // Drawer dashboard (src/ui/dashboard_renderer.cpp): clock header and tiles.
    void drawDashboard(float x);
    // focus_frame: draw the focused tile's own highlight (false while the gliding frame is drawn).
    void drawDashboardTile(int index, float drawer_left, bool focus_frame = true);
    void drawDashboardGlyph(int tile, float x, float y, D2D1_COLOR_F ink);
    void drawTileAvatars(float right, float center_y);
    void drawCategoryMenu();
    void drawCategoryRail();
    void drawQuickShade();
    void drawQuickPanel();
    void drawQuickGlyph(quick_menu::Action action, float x, float cy, D2D1_COLOR_F ink, int index, bool home);
    void drawFlixIcons(); // Flix (AppSettings::flix_top_icons): the quick bar's icons small, top right.
    void drawEmptyLibrary(); // The TV home without games: why, and its three buttons (empty_library.hpp).
    void drawGrid();
    void drawPluginPage(float left = 0, bool drawer = false);
    void drawDownloads(); // Pobierania (src/ui/downloads_renderer.cpp).
    void drawGlance();
    void drawGlanceTimelineCard(size_t index, settings_ui::Rect rect, float alpha, float weight);
    // Glance's free-standing home tile, or its Flix pill (home_widgets.hpp).
    void drawGlanceShortcut();
    // Towarzyszka (src/ui/companion_renderer.cpp): her free-standing tile or Flix pill.
    void drawCompanionTile();
    void drawCompanionPill(D2D1_RECT_F rect, float alpha, float hover);
    // Her picture inside one of the layouts' own tiles (Metro, XMB), clipped by `shape`: her head
    // `figure_height` tall, centred on `x`, standing on `bottom`; returns her mouth's height.
    float drawCompanionInTile(ID2D1Geometry* shape, float x, float bottom, float figure_height, float alpha);
    // Her home line beside such a tile (on the preferred side when there is room); remembered for clicks.
    void drawCompanionTileBubble(D2D1_RECT_F tile, float mouth_y, float alpha, bool prefer_left);
    // A frosted pill of the home's widgets (Flix), with its hover rim.
    void drawWidgetPill(D2D1_RECT_F rect, float alpha, float hover);
    void drawCompanion();
    void drawCompanionFeed();
    void drawCompanionPicker();
    // Gamification: a small "Lv 12" badge with a sympathy bar under it, starting at `left`,
    // centred on `cy`; returns its width (draw false: only measures). level 0: the host's from the
    // view (with her streak and level-up glow); else that level, and the bar only for progress ≥ 0.
    float drawCompanionAffinity(float left, float cy, float alpha, bool draw = true, int level = 0, float progress = 0);
    // The reset dialog of her "…" menu: the question and a red Zresetuj beside Anuluj.
    void drawCompanionResetDialog(const companion_ui::Layout& layout, const companion_ui::Chat& chat);
    // Her screens' questions (the reset dialog, "Usunąć to wspomnienie?"): a dim, the frosted panel
    // in `dialog`, the title, up to two lines under it, the red `action` pill beside Anuluj
    // (companion_ui::Layout::dialogButton). focus: confirm_reset/confirm_cancel; hover: confirmFocus(…).
    void drawCompanionQuestion(settings_ui::Rect dialog, const std::wstring& title, const std::wstring& body,
                               const std::wstring& action, int focus, int hover, float alpha = 1);
    // Her picture standing on (x, bottom), `height` tall, moved by her rig (companion_renderer.cpp).
    // `costume`: its slot (companion_costumes.hpp); a picture it lacks comes from her basic outfit.
    void drawCompanionFigure(companion::Look look, const avatar::Pose& pose, float x, float bottom, float height, float alpha, int costume = 0);
    // Her round avatar (desktop sidebar card, Flix pill): the settings cards' glow in her colour, her picture with
    // her face (companion_ui::faceOf) in the middle of the circle, a faint rim.
    void drawCompanionFace(companion::Look look, const avatar::Pose& pose, float cx, float cy, float radius, float alpha, int costume = 0);
    // Her anime marks around the head (anger vein, sweat drop, sparkles, "Zzz"…) for the feeling
    // she shows, in the coordinates of her picture `sprite` (src/ui/companion_marks.cpp).
    void drawCompanionMarks(const avatar::Pose& pose, D2D1_POINT_2F face, float face_size, float alpha);
    // The roulette card of her chat log, `width` wide with its bottom at `bottom`; returns its height.
    float drawCompanionRoulette(const companion_ui::Roulette& roulette, float left, float bottom, float width, float alpha);
    // A sprite of the executable's resources, decoded on first use; null until its bitmap is ready.
    Cover* companionSprite(companion::Look look, companion_ui::Frame frame, companion::Emotion emotion, int costume = 0);
    Cover* companionResource(int id); // Any embedded companion picture by its RCDATA ID.
    // Her full-figure pose in that costume (companion_ui::poseId), fitted into `area`, standing on its
    // bottom; returns whether the costume has one (it may still be decoding).
    bool drawCompanionPose(companion::Look look, int costume, settings_ui::Rect area, float alpha, float slide);
    // "…" → "Zmień strój": her costumes as tiles (companion_ui::Layout::wardrobe).
    void drawCompanionWardrobe(const companion_ui::Layout& layout, const companion_ui::Chat& chat);
    // "…" → "Umysł i pamięć": 3D holographic sphere on the left and telemetry/inspector panel on the right.
    void drawCompanionMind(const companion_ui::Layout& layout, const companion_ui::Chat& chat, float shown);
    // A hidden text masked like a spoiler: one capsule per word where the word would wrap (at most
    // `max_lines` lines from `top`), in `ink`, with a light sweeping across and twinkling grain.
    // Returns the height its lines take.
    float drawMaskedText(const std::wstring& value, float x, float top, float width, IDWriteTextFormat* format, int max_lines,
                         D2D1_COLOR_F ink, float alpha, uint32_t seed);
    // A small tracked caps label (label_font_), centred on `center_y`.
    void capsLine(const std::wstring& value, float x, float center_y, float width, D2D1_COLOR_F ink,
                  DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);
    void drawCompanionPreviews(const settings_ui::Layout& layout, const settings_ui::Row& row, int index, float scroll, bool focused);
    void drawCompanionCardGlow(D2D1_RECT_F rect, companion::Look look, float alpha = 1);
    void drawCompanionCardFocus(D2D1_RECT_F rect, int surface, float alpha = 1);
    Microsoft::WRL::ComPtr<ID2D1Geometry> companionCardShape(D2D1_RECT_F rect);
    struct CompanionText {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        float width = 24, height = 0;
    };
    CompanionText companionText(const std::wstring& value, const std::vector<companion::GameMention>& mentions, float width, int max_lines);
    void drawCompanionText(const CompanionText& text, const std::wstring& value, const std::vector<companion::GameMention>& mentions,
                           float x, float y, D2D1_COLOR_F ink, D2D1_RECT_F clip, companion::Look speaker);
    // `anchor`: the bubble's right edge (`left_of_anchor`) or its left edge.
    D2D1_RECT_F drawSpeechBubble(const std::wstring& value, bool typing, float anchor, float center_y, float max_width,
                                 float tip_x, float tip_y, float alpha, float min_top, bool left_of_anchor = true);
    void drawGlanceGlyph(float x, float y, float size, D2D1_COLOR_F ink);
    float drawGlanceText(const std::wstring& value, D2D1_RECT_F rect, IDWriteTextFormat* font, D2D1_COLOR_F ink);
    // Tarot (src/ui/tarot_renderer.cpp): the overlay, one card (face, back or a result's poster)
    // turned by `turn` (0 back … 1 face), its symbols, and a small card glyph for the top bar.
    void drawTarot();
    void drawTarotBackdrop(float alpha, float center_y, float radius);
    struct TarotCardLook {
        float cx = 0, cy = 0, width = 0, height = 0;
        float rotation = 0, turn = 1, lift = 0, glow = 0, alpha = 1;
    };
    void drawTarotCard(const TarotCardLook& look, const tarot::Card* face, Cover* poster, const std::wstring& numeral);
    void drawTarotCardBack(D2D1_RECT_F r, float alpha);
    void drawTarotSymbol(tarot::Symbol symbol, float cx, float cy, float size, D2D1_COLOR_F ink, float stroke);
    void drawTarotGlyph(float x, float y, float size, D2D1_COLOR_F ink);
    bool createTarotFonts();
    // Profil (src/ui/profile_renderer.cpp): the page, its cards and charts.
    void drawProfile();
    void drawAurelia();        // src/ui/aurelia_renderer.cpp: Tryb TV's dialog, or the desktop one below.
    void drawDesktopAurelia();
    // The cloud saves icon (Szczegoly, after the dots): a cloud, and a badge for synced or a problem.
    void drawCloudGlyph(float cx, float cy, D2D1_COLOR_F ink, std::optional<aurelia::CloudState> state);
    void drawQrCode(settings_ui::Rect r, const std::vector<uint8_t>& modules, int size); // aurelia_renderer.cpp
    bool createProfileFonts();
    void drawProfileImage(size_t index, D2D1_RECT_F box, float radius, float alpha, const std::wstring& fallback);
    void drawProfileCard(D2D1_RECT_F box, float alpha, float focus, float hover);
    void drawDownloadGlyph(float x, float cy, float scale, D2D1_COLOR_F ink); // An arrow into a tray.
    void drawSettings();
    void drawSettingsLayouts(const settings_ui::Layout& layout, int row, float scroll, bool focused);
    // Settings 2.0 (src/ui/settings_tiles_renderer.cpp): the sound source tiles and the controller
    // gallery of a tiled row, and the category icons of the rail.
    void drawSettingsTiles(const settings_ui::Layout& layout, const settings_ui::Row& row, int index, float scroll, bool focused);
    void drawSettingsIcon(int category, float cx, float cy, D2D1_COLOR_F ink);
    void drawPadTile(const settings_ui::Tile& tile, settings_ui::Rect rect, bool lit);
    void drawControllerGallery(); // The Kontrolery screen (device_renderer.cpp) with pads present.
    // A controller's picture fitted into `box`: the downloaded photo, else its drawn family.
    void drawControllerPicture(devices::Picture picture, D2D1_RECT_F box);
    // The whole scene drawn so far, blurred (glass) and dimmed: the backdrop of full-screen panels.
    void drawFrostedBackdrop(float dim);
    // Bottom-right button hints (src/ui/hints_renderer.cpp).
    void drawHints();
    float drawHintGlyph(const InputHint& hint, float x, float cy, float alpha); // Returns its width.
    // Home layout miniature (src/ui/settings_layout_renderer.cpp): option = AppSettings::HomeLayout,
    // time = seconds into its one-shot preview (0 = at rest), emphasis 1 for a focused or chosen tile.
    void drawLayoutScene(int option, D2D1_RECT_F picture, float time, float emphasis);
    // A layout tile: Settings (no description) or the walkthrough's large tiles (with one).
    void drawLayoutTile(int option, D2D1_RECT_F box, bool active, bool focus, float time, const std::wstring& description);
    // First-run walkthrough (src/ui/tour_renderer.cpp): dimmed screen with a spotlight, bubble and card.
    void drawTour();
    void drawTourCard(const TourView& tour);
    void drawTourBubble(const TourView& tour, D2D1_POINT_2F anchor, int side, float alpha);
    // The spotlight of a step (the opened panel as drawn this frame) and how far it has opened, 0..1.
    std::optional<D2D1_RECT_F> tourSpot(const TourView& tour, float& opened, D2D1_POINT_2F& anchor, int& side) const;
    // Wrapped text whose first line box starts at `top`; returns its height. draw = false only measures.
    float paragraph(const std::wstring& value, float x, float top, float width, IDWriteTextFormat* format,
                    D2D1_COLOR_F ink, int max_lines, bool draw = true);
    void drawDetails();
    void drawLibraryManager();
    void drawIdentify();
    void drawBulkCovers();
    float drawRating(float x, float y, float width, float alpha = 1);
    void drawAchievements(); // src/ui/achievements_renderer.cpp
    void drawWhatsNew();     // src/ui/update_renderer.cpp
    void drawAchievementCard(const achievements_ui::State& state, const achievements_ui::Layout& layout);
    // Desktop: the same card over the Osiągnięcia page of the Szczegóły sheet.
    void drawDesktopAchievementCard(const achievements_ui::State& state);
    void drawArtworkBrowser();
    void drawDevices();
    void drawAudioDevices();
    void drawDeviceFocus(bool editing); // Gliding focus frame of the devices screen, if any.
    void drawCropEditor();
    void drawGogInstall();
    void drawCustomGameEditor();
    void drawFileBrowser();
    void drawKeyboard(); // On-screen keyboard (keyboard_renderer.cpp).
    void drawFileGlyph(file_browser::Kind kind, float cx, float cy, D2D1_COLOR_F ink);
    void drawPlaceGlyph(file_browser::Icon icon, float cx, float cy, D2D1_COLOR_F ink);
    void drawInstallDisc(float cx, float cy, float radius, float opacity, bool moving);
    void drawLibraryControls();
    float libraryBarAlpha() const; // The library top bar (tabs, filters, magnifier): fades as the grid scrolls.
    void drawSearchGlyph(float cx, float cy, float radius, D2D1_COLOR_F ink, float stroke);
    void drawFilterMenu();
    void drawViewDrawer();
    // Library list view (src/ui/library_renderer.cpp): rows and the focused game's panel.
    void drawLibraryList();
    void drawLibraryPreview();
    void drawLibraryListPlus();
    void drawLibraryDescription(settings_ui::Rect rect, float alpha);
    void drawLibraryFacts(settings_ui::Rect rect, float alpha, bool metadata);
    void drawHeroFacts(float center, float top, float min_left, float alpha);
    void drawLibraryCinematic();
    bool drawCinematicFacts(float x, float center_y, float width, float alpha);
    void drawLibraryTabs();     // Kinowy / Przegląd: the category strip.
    void drawLibraryOverview(); // Przegląd: tiles, then drawOverviewPanel.
    void drawOverviewPanel();
    void drawLibraryDetailed();
    void drawListPlusPoster();
    void drawLibraryActions();
    void drawLibraryLogo(D2D1_RECT_F rect, float alpha);
    void drawLibraryAmbient(float opacity);
    void drawLibraryCoverflow();
    void drawCoverflowDisc(const GameCard& card, size_t index);
    void drawCoverflowPoster(const GameCard& card, size_t index);
    void drawCdCase(const GameCard& card, size_t index);
    // Półka (src/ui/shelf_renderer.cpp): Cover Flow in perspective around ViewContext::library_flow.
    void drawLibraryShelf();
    bool createShelfResources();
    ID2D1Bitmap* blurredArt(Cover& image); // Cover::blurred, made on first use.
    ID2D1Bitmap* desaturatedArt(Cover& image); // Cover::desaturated (Saturation effect), else the WIC grey copy.
    // Półka: a spine's print (the name; with a band, the platform's colour and logo at its top), cached.
    ID2D1Bitmap* shelfSpine(const std::wstring& name, const std::wstring& platform, std::optional<D2D1_COLOR_F> band);
    void drawMediaCard(const GameCard& card, size_t index); // Kartridż Switch.
    void drawFloppy(const GameCard& card, size_t index);
    void createMediaMaterials();
    void createFloppyMaterials();
    // sweep: coverflow_layout::Layout::lightSweep, the light's slide while the case glides.
    void drawSteelbook(Cover& image, D2D1_RECT_F rect, ID2D1Geometry* clip, float opacity, float sweep);
    void uploadSteelbook(Cover& image);
    void createSteelbookSurface();
    void createSteelbookStreak();
    void drawScrollLetter();
    // Flix home (src/ui/flix_renderer.cpp).
    void drawFlix();
    void drawRectangularCard(const GameCard& card, size_t index, float alpha);
    void drawFlixUnfolded(const GameCard& card, size_t index, float alpha); // Flix's wide card (flix_layout::unfold_rest).
    // Metro home (src/ui/metro_renderer.cpp): tabs over pages of tiles.
    void drawMetro();
    void drawMetroTabs();
    void drawMetroTile(const GameCard& card, size_t index);
    void drawMetroGlyph(metro_layout::Role role, float cx, float cy, float size, D2D1_COLOR_F ink);
    // PSfive home (src/ui/psfive_renderer.cpp): a row of tiles over the full-screen background,
    // the focused game's badge and name, "Graj" and "…", its logo in the corner.
    void drawPsfive();
    void drawSnitch();
    void drawPsfiveTopBar();
    void drawPsfiveMoreMenu();
    void drawPsfiveLabel();
    void drawPsfiveLogo(float lift); // Over "Graj" / "…"; the title in large type without a logo.
    void drawPsfiveButtons();
    // The focused tile's light frame, a thin dark gap outside its edge (as on the PS5).
    void drawPsfiveFrame(float x, float y, float size, D2D1_COLOR_F tint, float alpha);
    // Frosted glass (src/ui/glass_renderer.cpp). frostCapture copies the scene drawn so far under
    // `shape` (in the current transform, plus the blur's reach) and blurs it by `blur` DIPs;
    // frostDraw puts that copy back where it came from, inside `shape`. Between them a dialog
    // may dim the screen: its panel still shows the clear scene, blurred. False without Direct2D
    // 1.1 effects, on the layered Guide overlay (nothing behind it to blur) or when the copy fails.
    bool frostCapture(ID2D1Geometry* shape, float blur);
    void frostDraw(ID2D1Geometry* shape, float opacity = 1);
    // The scene under a side drawer, frosted at `opacity`; false without glass.
    bool frostPane(D2D1_RECT_F pane, float opacity);
    // A dialog or menu panel on frosted glass: glassPanel before the dim, glassFill after it (the
    // blurred scene, then `tint`, lighter with glass). Without glass the tint is drawn as given.
    struct Glass { Microsoft::WRL::ComPtr<ID2D1Geometry> shape; bool frosted = false; };
    Glass glassPanel(D2D1_ROUNDED_RECT panel, float blur = 16);
    void glassFill(const Glass& glass, D2D1_COLOR_F tint, float opacity = 1);
    // Buttons (glass_renderer.cpp): every button is a pill (radius = half its height; a square one
    // a circle) after PSfive's "Graj": grey frosted glass, no outline; the focused one
    // solid white with dark ink. `frosted` blurs the scene under it (over a live picture);
    // false over a panel or backdrop that is frosted already (Settings, dialogs).
    static D2D1_ROUNDED_RECT pill(D2D1_RECT_F r) {
        const float radius = std::max(0.0f, std::min(r.right - r.left, r.bottom - r.top) * 0.5f);
        return D2D1::RoundedRect(r, radius, radius);
    }
    // Back ("a frame with a white dot"): a circle as tall as its hit rectangle, at its left edge.
    static D2D1_RECT_F backCircle(float left, float top, float right, float bottom) {
        return D2D1::RectF(left, top, left + std::min(bottom - top, right - left), bottom);
    }
    // The glass of an unfocused button. hovered: a lighter tint under the pointer.
    void drawButtonGlass(D2D1_RECT_F r, float alpha = 1, bool frosted = true, bool hovered = false, bool enabled = true);
    // The focused button's white pill (it may glide between buttons on its own rectangle).
    void drawButtonFocus(D2D1_RECT_F r, float alpha = 1);
    // Glass, then the white pill faded in by `focus` (0..1). Returns the caption's ink.
    D2D1_COLOR_F drawButton(D2D1_RECT_F r, float focus, float alpha = 1, bool frosted = true, bool hovered = false, bool enabled = true);
    // The caption's ink: light on the idle glass, dark on a light focused fill (and the other way
    // round for dark or saturated themes), greyed when disabled.
    D2D1_COLOR_F buttonInk(float focus, bool enabled = true, float alpha = 1) const;
    // The focused fill and the idle tint of the button theme (setButtonTheme).
    D2D1_COLOR_F buttonFill(float alpha = 1) const;
    D2D1_COLOR_F buttonIdleTint(bool hovered, bool enabled, float alpha) const;
    // The outward offset of a focus outline `width` wide around its tile: half the stroke keeps it
    // outside, the gap (its width again) leaves an empty ring when focus_gap_ is on.
    float focusOutset(float width) const { return focus_gap_ == AppSettings::FocusGap::outside ? width * 1.5f : 0.0f; }
    // "Grafika do środka": the outline stays on the tile's edge and the picture shrinks by this much on
    // every side (the stroke's inner half and the gap, as wide as the stroke).
    float focusInset(float width) const { return focus_gap_ == AppSettings::FocusGap::inside ? width * 1.5f : 0.0f; }
    // Small glyph caps (button hints, LB/RB, key captions): a light translucent pill, no outline.
    void drawKeyCap(D2D1_RECT_F r, float alpha = 1, bool enabled = true);
    // Black on white: the platform's logo (its alpha as the ink) or its name; returns the width.
    float drawPsfiveBadge(const std::wstring& platform, float x, float center_y, float alpha);
    // Desktop mode (src/ui/desktop_renderer.cpp and its desktop_*_renderer.cpp siblings).
    void drawDesktop();
    void drawDesktopTitlebar(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopSidebar(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopCompanion(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopGrid(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopHeader(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopEmpty(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopPoster(const desktop_ui::Layout& layout, const desktop_ui::View& view, size_t index);
    void drawDesktopPane(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopBulk(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopDownloads(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopMenu(const desktop_ui::Layout& layout, const desktop_ui::Menu& menu);
    void drawDesktopMenuPanel(settings_ui::Rect panel, const std::vector<desktop_ui::MenuItem>& items, int hover, int open);
    void drawDesktopDrag(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawDesktopTooltip(const desktop_ui::Layout& layout, const desktop_ui::View& view);
    void drawWindowStrip(); // Window buttons over a TV screen inside the desktop window.
    // Desktop dialogs (src/ui/desktop_settings_renderer.cpp, desktop_sheet.hpp).
    void drawDesktopWindowBar(const desktop_ui::SheetView& view, bool background = true);
    void captureDesktopBackdrop();
    bool drawDesktopBackdrop(float dim); // The library behind a dialog or page; false before there is one.
    void drawDesktopSheetFrame(const desktop_ui::Sheet& sheet, const desktop_ui::SheetView& view, const std::wstring& title, bool back);
    void drawDesktopSettings();
    void drawDesktopDetails();
    void drawDesktopAchievements(const achievements_ui::State& list, float left, float right, float top);
    void drawDesktopArtwork();
    void drawDesktopDownloadsPage(); // src/ui/desktop_pages_renderer.cpp
    void drawDesktopGlance();
    void drawDesktopStore();
    void drawDesktopEmbedded(); // Urządzenia, kadrowanie, plugin pages inside a desktop sheet.
    void desktopHot(desktop_ui::SheetHit hit, settings_ui::Rect rect) { desktop_hotspots_.push_back({hit, rect}); }
    // Desktop dialogs over desktop pages (src/ui/desktop_dialogs_renderer.cpp).
    bool desktopDialogs() const;
    settings_ui::Rect desktopDialogFrame(const Viewport& view, float width, float height, const std::wstring& title, bool back);
    void desktopTextField(settings_ui::Rect field, const std::wstring& value, const std::wstring& placeholder, size_t caret, bool focused,
                          bool selected, float hover);
    void drawDesktopCollectionsDialog();
    void drawDesktopCustomEditor();
    void drawDesktopIdentify();
    void bindCustomPoster(); // ViewContext::custom_poster into custom_poster_ (decoded once per picture).
    void drawDesktopPrompts(); // Zasilanie and the question before closing.
    void drawDesktopGogInstall();
    void desktopModalHot(desktop_ui::SheetHit hit, settings_ui::Rect rect) { desktop_modal_hotspots_.push_back({hit, rect}); }
    void desktopSwitch(settings_ui::Rect r, float on, bool locked, float hover);
    void desktopField(settings_ui::Rect r, const std::wstring& value, wchar_t glyph, float hover, bool open, uint32_t swatch, bool has_swatch);
    // Drawing helpers: an icon-font glyph centred on (cx, cy), filled/stroked rounded rectangles,
    // a soft drop shadow, the keyboard focus ring and a button (0 ghost, 1 outline, 2 primary).
    void desktopGlyph(wchar_t glyph, float cx, float cy, float size, D2D1_COLOR_F ink, float angle = 0);
    bool desktopBrand(std::string_view key, float cx, float cy, float size, D2D1_COLOR_F ink);
    // launchme's full logo (symbol and wordmark, res/launchme_logo.svg) in one colour, `height` tall (the symbol's
    // height), its left edge at `left` (or centred on it), centred on `cy`; returns its width (0 if it cannot be built).
    // `mark_only`: the portal symbol alone (the narrow desktop sidebar).
    float drawLaunchmeLogo(float left, float cy, float height, D2D1_COLOR_F ink, bool centered = false, bool mark_only = false);
    // A cached brand geometry (brand_icons_) filled under `transform`, tessellated once per device scale.
    void fillBrandGeometry(const std::string& key, ID2D1Geometry* geometry, const D2D1_MATRIX_3X2_F& transform, D2D1_COLOR_F ink);
    void drawDesktopQuick(const desktop_ui::View& view);
    void drawDesktopMeta(const desktop_ui::MetaView& meta); // Also Tryb TV's, from the game's menu.
    void desktopSelection(const std::wstring& value, float left, float cy, float width, IDWriteTextFormat* format); // Its highlight, if any.
    void desktopFill(settings_ui::Rect r, float radius, D2D1_COLOR_F ink);
    void desktopStroke(settings_ui::Rect r, float radius, D2D1_COLOR_F ink, float width = 1);
    void desktopShadow(settings_ui::Rect r, float radius, float strength);
    void desktopFocusRing(settings_ui::Rect r, float radius);
    void desktopButton(settings_ui::Rect r, int style, float hover, bool pressed, bool enabled, wchar_t glyph, const std::wstring& label);
    // The primary button as an installation: its share fills it (an unknown one sweeps), its state the label.
    void desktopInstallButton(settings_ui::Rect r, float fraction, const std::wstring& label); // fraction < 0: unknown.
    void desktopArc(float cx, float cy, float radius, float start, float sweep, D2D1_COLOR_F ink, float width);
    float desktopCenteredText(const std::wstring& value, float cx, float top, float width, IDWriteTextFormat* format, D2D1_COLOR_F ink, int max_lines);
    void desktopBitmapFill(ID2D1Bitmap* bitmap, D2D1_RECT_F box, float opacity);
    void drawXmb();
    void drawXmbCaption();
    void drawXmbLabels();
    // The focused game's logo in a bottom-right box at most max_width x max_height whose right
    // and bottom edges it touches (trimmed to its ink bounds). XMB: above the platform badge.
    std::optional<D2D1_RECT_F> cornerLogoRect(float max_width, float max_height, float right, float bottom) const;
    void drawCornerLogo(std::optional<D2D1_RECT_F> box);
    std::optional<D2D1_RECT_F> xmbLogoRect() const;
    void drawXmbLogo() { drawCornerLogo(xmbLogoRect()); }
    void drawXmbTileIcon(const GameCard& card, size_t index, float x, float y, float size, float transparency);
    float xmbTransparency(size_t index, bool selected) const;
    float focusY() const;
    int distance(size_t index) const;
    // Screensaver (src/ui/screensaver_renderer.cpp). saver_alpha_ is the intro fade.
    void drawScreensaver(const screensaver::View& view);
    void drawSaverGlow(D2D1_COLOR_F ink, float cx, float cy, float radius, float alpha);
    void drawSaverHero(const screensaver::View::Item& item, float alpha);
    void drawSaverBox(const screensaver::View::Item& item, float alpha);
    void drawSaverFlatBox(const screensaver::View::Item& item, float alpha);
    void drawSaverDisc(const screensaver::View::Item& item, float alpha);
    void drawSaverTitle(const std::wstring& title, float alpha);
    bool createSaverResources();

    HWND hwnd_ = nullptr;
    bool layered_ = false;
    bool low_memory_ = false, block_compress_ = false; // setLowMemory.
    bool bc1_supported_ = false; // The target samples BC1 bitmaps (ID2D1DeviceContext, Windows 8.1+).
    bool bc3_supported_ = false; // BC3 too (logos with transparency); not on Linux.
#ifdef _WIN32
    SwapChainTarget swap_;    // The window's target (createTarget); empty: the HWND target below or none.
    bool swap_wanted_ = true; // LAUNCHME_SWAPCHAIN=0: the HWND target, for comparisons.
    bool swap_failed_ = false; // Could not be made: the HWND target until releaseGraphics.
    bool tearing_wanted_ = false; // LAUNCHME_PRESENT_IMMEDIATE=1 (the uncapped benchmark).
    bool gpu_video_ = true;   // LAUNCHME_VIDEO_GPU=0: video frames copied through memory.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> video_texture_; // videoTexture, and its bitmap until the first frame shows.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> video_texture_bitmap_;
#endif
    uint64_t device_generation_ = 0;
    std::chrono::steady_clock::time_point caches_released_{};
    bool art_lost_ = false; // takeArtLost.
    LayeredSurface layered_surface_;
    ImageDecoder decoder_;
    uint64_t decoded_seen_ = 0;
    bool uploads_left_ = false; // The last frame's upload budget left finished decodes for the next.
    std::chrono::steady_clock::time_point last_reveal_{};
    bool profiling_ = false;
    bool immediate_present_ = false;
    RenderTiming timing_;
    double pending_upload_ms_ = 0;
    unsigned pending_uploads_ = 0;
    Viewport viewport_;
    DrawerStyle drawer_style_;
    AppSettings::TileShape tile_shape_ = AppSettings::TileShape::squircle;
    // Frames around dialogs, context menus and the walkthrough (Settings → Appearance).
    bool panel_borders_ = AppSettings{}.panel_borders;
    bool console_colors_ = AppSettings{}.console_colors; // Settings → Interfejs → Kolorowe kafelki konsol.
    float glass_level_ = AppSettings{}.glass;
    // Scales of the frosted glass: the blur radius and how much of the tint stays over it.
    float glassBlur(float blur) const { return blur * (0.6f + 0.8f * glass_level_); }
    float glassTint() const { return std::lerp(0.92f, 0.5f, glass_level_); }
    std::wstring font_family_ = L"SST";
    TextLayoutCache text_layouts_;
    // TextLayoutCache variants: textAdvance's unwrapped measure, textLine (+1 + alignment override),
    // desktopCenteredText and the desktop icon glyphs; 0 is text()'s plain layout.
    static constexpr uint32_t text_variant_advance = 1, text_variant_line = 16, text_variant_centered = 32, text_variant_glyph = 48;
    uint32_t cover_accent_ = 0; // 0 = dominant poster colour.
    uint32_t ui_accent_ = AppSettings{}.ui_accent_rgb;
    uint32_t button_rgb_ = AppSettings{}.button_rgb;
    AppSettings::ButtonIdle button_idle_ = AppSettings{}.button_idle;
    AppSettings::FocusGap focus_gap_ = AppSettings{}.focus_gap;
    float glow_level_ = AppSettings{}.glow_level;
    bool glow_pulse_ = AppSettings{}.glow_pulse;
    bool glow_trace_ = AppSettings{}.glow_trace;
    float background_grid_ = AppSettings{}.background_grid;
    AppSettings::GridStyle background_grid_style_ = AppSettings{}.background_grid_style;
    // Focus outline of covers and tiles: the pulse phase (steady at its middle without pulsing),
    // the halo alpha scale and a halo stroke width (wider above 100%, never narrower than 60%).
    float glowPulse() const { return glow_pulse_ ? 0.5f + 0.5f * std::sin(elapsed_ * 2.5f) : 0.5f; }
    float glowAlpha() const { return glow_level_; }
    float glowWidth(float width) const { return width * (0.6f + 0.4f * std::max(glow_level_, 1.0f)); }
    D2D1_COLOR_F uiAccent(float alpha = 1) const {
        return D2D1::ColorF(((ui_accent_ >> 16) & 255) / 255.0f, ((ui_accent_ >> 8) & 255) / 255.0f,
                            (ui_accent_ & 255) / 255.0f, alpha);
    }
    const std::vector<GameCard>* cards_ = nullptr; // Borrowed only during paint().
    const ViewContext* context_ = nullptr;
    size_t selected_ = 0;
    bool scanning_ = true;
    // First frame of the startup library scan: its placeholder waits, so a quick scan shows none.
    std::chrono::steady_clock::time_point scanning_since_{};
    float elapsed_ = 0;
    float detail_ = 0;
    std::vector<Cover> covers_;
    std::vector<Cover> logos_;
    std::vector<Cover> heroes_;
    std::vector<Cover> artwork_thumbnails_;
    Cover file_browser_preview_;
    Cover notification_cover_;
    Cover custom_poster_; // Dodaj / Edytuj grę: the poster beside the fields (ViewContext::custom_poster).
    Cover companion_photo_; // The picture she sent in the chat.
    std::array<SpringBox, 3> companion_card_focus_;
    std::array<float, 3> companion_card_frame_{-1, -1, -1}; // TV settings, desktop settings, picker.
    // Settings → Asystent: the cards' slider gliding to its first card (fractional), and when it last moved.
    float companion_strip_first_ = 0, companion_strip_time_ = -1;
    Cover companion_photo_large_; // The same, decoded larger once it is enlarged.
    std::map<std::string, Cover> companion_feed_images_; // Only the phone's visible neighbourhood.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> feed_body_font_, feed_small_font_, feed_title_font_;
    std::vector<Cover> plugin_thumbnails_; // Drawer images, independent of underlying library cards.
    std::vector<Cover> glance_thumbnails_;
    std::vector<Cover> tarot_images_; // Tarot's result cards.
    std::vector<bool> tarot_posters_; // Per result: the image is its poster (else a cover standing in).
    std::vector<Cover> profile_images_; // Profil's pictures (Launcher::profile_slots_).
    Microsoft::WRL::ComPtr<IDWriteTextFormat> profile_name_font_, profile_value_font_, profile_big_font_, profile_title_font_, profile_body_font_, profile_small_font_;
    std::wstring profile_fonts_family_;
    // Tarot's type: a calligraphic title, serif card names and numerals, an italic question.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> tarot_title_font_, tarot_name_font_, tarot_numeral_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> tarot_question_font_, tarot_body_font_, tarot_small_font_, tarot_result_font_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> tarot_stroke_; // Round caps and joins for the card symbols.
    std::unordered_map<std::string, Cover> widget_icons_;
    std::vector<std::array<Cover, 4>> tile_thumbnails_; // Indexed like covers_.
    std::vector<Cover> posters_;                        // Flix posters/capsules, indexed like covers_.
    std::vector<Cover> discs_;                          // Coverflow: selected, retracting, and one look-ahead.
    settings_ui::Rect details_achievements_{};           // Set by drawDetails().
    settings_ui::Rect details_comparison_{};
    std::unordered_map<uint64_t, Cover> avatars_;
    settings_ui::Rect rating_link_{};
    // By companion_ui::spriteId (look, frame, emotion, costume); `missing` remembers a frame the
    // executable does not contain.
    struct CompanionSprite { Cover cover; bool missing = false; };
    std::unordered_map<int, CompanionSprite> companion_sprites_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> companion_stroke_; // Round caps and joins for her marks.
    settings_ui::Rect companion_bubble_{};
    settings_ui::Rect companion_photo_rect_{};
    struct CompanionLink { settings_ui::Rect rect; std::wstring title; };
    std::vector<CompanionLink> companion_links_;
    std::vector<CompanionLink> companion_videos_;
    // Her Metro tile as drawn this frame, for her bubble over the other tiles.
    struct TileMouth { D2D1_RECT_F tile; float mouth, alpha; };
    std::optional<TileMouth> companion_tile_mouth_;
    float companion_scroll_limit_ = 0;
    std::unordered_map<std::wstring, Cover> platform_badges_;
    // The background picture on screen and the one fading out, followed by their encoded bytes:
    // a rebuilt shelf or a new decode of the same picture continues it from its visible level
    // instead of cross-fading it into a shifted copy of itself.
    struct HeroLayer {
        ImageBytes pixels;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        HeroTone tone = HeroTone::full;
        float level = 0;       // Visible share, 0..1.
        CropFocus focus;       // Its slot's focus (Cover::focus).
    };
    HeroLayer hero_shown_, hero_leaving_;
    // The cross-fade from hero_leaving_ to hero_shown_, linear 0..1 (1: settled). It advances with
    // the frame clock or with detail_'s growth, whichever is more (a test sets detail_ directly).
    float hero_mix_ = 1;
    float hero_detail_seen_ = 0;
    std::chrono::steady_clock::time_point hero_frame_{};
    bool hero_waiting_ = false; // A ready picture waits for the focus to settle or a fade to finish.
    Cover kept_hero_; // One selected hero (including an in-flight decode) across a card rebuild.
    ScreenMode painted_screen_ = ScreenMode::shelves;
    bool screen_changed_ = false;
    ScreenMode background_screen_ = ScreenMode::shelves;
    float full_screen_from_ = 0, full_screen_mix_ = 0;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> main_target_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext1> main_realizer_;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> screen_ui_target_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> video_bitmap_;
    Microsoft::WRL::ComPtr<ID2D1Effect> video_blur_; // Fill behind 4:3 videos.
    std::chrono::steady_clock::time_point video_started_{};
    Microsoft::WRL::ComPtr<ID2D1Bitmap> cinematic_hero_, cinematic_previous_hero_;
    std::chrono::steady_clock::time_point cinematic_changed_{};
    bool cinematic_waiting_ = false; // A settled picture waits for the cross-fade under way.
    std::wstring platform_aura_name_;
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> platform_aura_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> steelbook_edge_;
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> steelbook_light_, steelbook_sheen_, steelbook_haze_;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> steelbook_streak_; // A8 smooth anisotropic band across the brush lines.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> steelbook_surface_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> media_plastic_, media_edge_;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> media_grain_;
    // Dyskietka: textured plastic, brushed steel (both procedural tiles), the light along the
    // shutter, its bevel and the paper of the label (createFloppyMaterials).
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> floppy_plastic_, floppy_metal_, floppy_paper_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> floppy_sheen_, floppy_bevel_, floppy_media_;
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> floppy_glint_;
    // Półka (shelf_renderer.cpp): the floor under the reflections, the reflections' fade, a face's
    // shading and gloss, and the light behind the focused cover.
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> shelf_floor_, shelf_fade_, shelf_shade_, shelf_gloss_;
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> shelf_glow_;
    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> shelf_spines_; // shelfSpine, by content.
    std::wstring marquee_text_; std::chrono::steady_clock::time_point marquee_since_{}; bool marquee_active_ = false; // fadingLine.
    uint64_t slot_generation_ = 0; // resetCovers/keepCovers/remapCovers.
    // Procedural backdrop for the All games tile, blended with the hero transition.
    bool library_backdrop_active_ = false;
    float library_backdrop_from_ = 0, library_backdrop_opacity_ = 0;
    // Four colour palettes x three ribbons; brushes are made as a palette enters.
    std::array<Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush>, 12> library_glows_;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> window_target_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> exit_frame_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> screen_frame_;
    // The library's last frame before a page opened over it (armed by Launcher::desktopLeaveLibrary): the
    // page sits over it, barely dimmed. Stale once the library is drawn again.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> desktop_backdrop_;
    bool desktop_backdrop_armed_ = false, desktop_backdrop_fresh_ = false;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Geometry>> brand_icons_; // Built once (device-independent).
    // Tessellated once per mark and device scale (ID2D1DeviceContext1), not on every frame.
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1GeometryRealization>> brand_realizations_;
    // Desktop posters: the tile shape tessellated once per poster size, tile shape and scale
    // (every poster of the grid has the same size) — its fill, 1-DIP outline and the four soft
    // shadow layers — and drawn translated, instead of new geometry on every frame.
    struct PosterShape {
        Microsoft::WRL::ComPtr<ID2D1Geometry> geometry; // At the origin, the poster's size.
        Microsoft::WRL::ComPtr<ID2D1GeometryRealization> fill, outline;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> shadow; // The four layers at full strength, drawn at the strength wanted.
        D2D1_RECT_F shadow_box{};                   // Where it lies around the poster's origin (DIPs).
        uint64_t used = 0;
    };
    std::unordered_map<uint64_t, PosterShape> poster_shapes_;
    uint64_t poster_shape_clock_ = 0;
    const PosterShape* posterShape(float width, float height);
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> desktop_shade_, desktop_fade_top_, desktop_fade_bottom_, desktop_hero_fade_;
    ID2D1LinearGradientBrush* desktopGradient(Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>& cached, D2D1_COLOR_F from, D2D1_COLOR_F to, float y0, float y1);
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> screen_bow_target_;
    bool exit_capture_attempted_ = false;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> exit_line_;
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> exit_glow_;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> squircle_;
    // Unit geometry: less than a quarter pixel of flattening error at 4096 px.
    static constexpr float squircle_tolerance = D2D1_DEFAULT_FLATTENING_TOLERANCE / 4096.0f;
    // XMB and Flix: smaller squircle corners (see compact_corner in renderer.cpp). The unit square
    // for cards drawn in unit space, and per-aspect unit shapes for rectangular tiles.
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> squircle_compact_;
    mutable std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID2D1PathGeometry>> compact_shapes_; // By aspect * 256.
    mutable std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID2D1PathGeometry>> capped_shapes_; // By aspect and corner.
    float corner_reference_ = 0; // Metro: tileShape rounds corners as for this shorter side at most; 0 = off.
    float squircle_corner_ = 0.5f; // Corner / shorter side of the tiles being drawn; 0.5 = full squircle.
    // Where the screen's canvas sits in the target: a TV screen inside a desktop sheet
    // (drawDesktopEmbedded) is moved to the sheet's body. Code that sets its own transform for a
    // card (drawCard) starts from canvasBase(), never from the bare scale.
    D2D1::Matrix3x2F canvas_offset_ = D2D1::Matrix3x2F::Identity();
    D2D1::Matrix3x2F canvasBase() const { return D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale) * canvas_offset_; }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> power_arc_; // Quick bar power glyph: open circle around the origin.
    // Device-dependent: made with the target, released with it.
    Microsoft::WRL::ComPtr<ID2D1DeviceContext1> realizer_;
    Microsoft::WRL::ComPtr<ID2D1GeometryRealization> squircle_fill_;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID2D1GeometryRealization>> squircle_strokes_; // By quantised width.
    Microsoft::WRL::ComPtr<ID2D1GeometryRealization> squircle_compact_fill_;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID2D1GeometryRealization>> squircle_compact_strokes_;
    Microsoft::WRL::ComPtr<ID2D1Layer> quick_layer_; // Clips the dark icon pass to the quick bar circle.
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> controller_glyph_;
    // Device-independent: built on first use per family, kept across device loss.
    std::unordered_map<int, ControllerArt> controller_art_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> controller_art_stroke_;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> play_glyph_; // Unit triangle around the origin.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solid_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> shade_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> install_vignette_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> status_shade_;
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> aura_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> backdrop_fade_;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> grid_mask_; // drawBackgroundGrid's dot tile.
    int grid_mask_cell_ = 0;
    AppSettings::GridStyle grid_mask_style_ = AppSettings::GridStyle::dots;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> friends_backdrop_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> category_backdrop_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> category_rail_mask_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> quick_fade_; // Black rising from the bottom edge under the quick bar.
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> quick_ring_; // Quick bar focus ring: bright at the start point, faint opposite.
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> psfive_shade_; // PSfive: the background darker under the row and the buttons.
    // Frosted glass: copies of the scene by size (the PSfive buttons and a dialog differ every
    // frame), the blur over the last one and where it was copied from (device pixels).
    std::array<Microsoft::WRL::ComPtr<ID2D1Bitmap>, 4> frost_bitmaps_;
    size_t frost_next_ = 0;
    Microsoft::WRL::ComPtr<ID2D1Effect> frost_blur_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> frost_foreground_;
    Microsoft::WRL::ComPtr<ID2D1Effect> frost_composite_;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> orbit_target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> orbit_ink_;
    Microsoft::WRL::ComPtr<ID2D1Effect> orbit_blur_;
    D2D1_POINT_2F frost_origin_{};
    bool frost_ready_ = false;
    // Metro shortcut tiles: Biblioteka, Platformy PC, Emulacja, Ustawienia (renderer_resources.cpp).
    std::array<Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>, 6> metro_gradients_;
    std::vector<settings_ui::Rect> metro_tab_rects_; // As drawn last; one per page.
    std::vector<settings_ui::Rect> psfive_tab_rects_; // As drawn last; one per PSfive tab.
    Microsoft::WRL::ComPtr<ID2D1Layer> category_rail_layer_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IDWriteFontCollection1> sst_fonts_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> hero_font_, label_font_, cd_pen_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> button_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> action_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> category_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> menu_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> sidebar_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> category_active_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> friend_name_font_, friend_status_font_, friend_section_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> toast_font_, toast_sub_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> settings_heading_font_, settings_body_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> details_body_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> clock_font_, clock_date_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> metro_tab_font_; // Metro page tabs.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> psfive_name_font_; // PSfive: the focused game's name beside its badge.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> psfive_clock_font_; // PSfive: the clock in the top bar.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> psfive_tab_font_, psfive_tab_idle_font_; // PSfive tabs: active, the others.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> psfive_button_font_; // PSfive "Graj".
    Microsoft::WRL::ComPtr<IDWriteTextFormat> tile_value_font_; // All widget values/statuses, wrapping.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> index_font_; // Quick scroll initial, centred.
    // Desktop mode: compact type for the mouse and the icon font (Segoe Fluent Icons / MDL2 Assets).
    Microsoft::WRL::ComPtr<IDWriteTextFormat> desktop_font_, desktop_strong_font_, desktop_small_font_, desktop_label_font_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> desktop_title_font_, desktop_heading_font_, desktop_button_font_, desktop_icon_font_;
    bool createDesktopFonts();
    bool desktop_viewport_ = false;
    float desktop_pane_height_ = 0;
    std::vector<settings_ui::Rect> desktop_chips_;
    settings_ui::Rect desktop_chip_add_{}, desktop_rating_{}, desktop_protondb_{}, desktop_achievements_{}, desktop_cloud_{};
    std::chrono::steady_clock::time_point skeleton_at_{}; // The last frame that drew a skeleton.
    std::array<settings_ui::Rect, 2> desktop_studios_{};
    std::vector<desktop_ui::SheetHotspot> desktop_hotspots_;
    desktop_ui::DialogKind desktop_modal_ = desktop_ui::DialogKind::none;
    std::vector<desktop_ui::SheetHotspot> desktop_modal_hotspots_;
    // The desktop window (ViewContext::desktop_window): Tryb TV's dialogs and screens drawn with
    // solid panels, outlined buttons and plain dimming instead of glass and pills.
    bool desktop_style_ = false;
    Viewport desktopModalViewport() const; // Tryb TV's dialogs over desktop pages: a compact canvas.
    float desktop_sheet_height_ = 0;
    // The pane's background picture and the one it replaces, cross-faded.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> desktop_hero_, desktop_hero_previous_;
    std::chrono::steady_clock::time_point desktop_hero_changed_{};
    std::unordered_map<IDWriteTextFormat*, TypeMetrics> type_metrics_; // Cleared with the fonts.
    // Screensaver: image slots and resources made on first use, released with the target.
    std::array<Cover, screensaver_slots> saver_slots_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> saver_context_; // Perspective DrawBitmap (Windows 8+).
    bool saver_context_checked_ = false;
    bool perspective_boxes_ = false;
    float saver_alpha_ = 1;
    Microsoft::WRL::ComPtr<ID2D1Layer> saver_layer_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> saver_mask_, saver_sheen_, saver_shade_;
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> saver_shadow_;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush>> saver_glows_; // By RGB.
};
