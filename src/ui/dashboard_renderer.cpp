// Drawer dashboard: clock header and the Steam friends / Discord / CPU / GPU tiles.
#include "renderer.hpp"
#include "friends_layout.hpp"

#include <d2d1helper.h>

#include <algorithm>
#include <cmath>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace dashboard = friends_layout::dashboard;

namespace {
using palette::gray;
bool online(const SteamFriend& person) { return person.status > 0 || !person.game.empty(); }
constexpr float tile_padding = 20.0f;
constexpr int hot_celsius = 85; // Shown in the soft warning red used for destructive actions.
std::wstring fpsNumber(double value) { return value > 0 ? std::to_wstring(static_cast<int>(std::lround(value))) : L"—"; }
const wchar_t* fpsStatus(game_fps::Status value) {
    switch (value) {
    case game_fps::Status::denied: return tr(L"Brak uprawnień ETW");
    case game_fps::Status::failed: return tr(L"Pomiar FPS niedostępny");
    case game_fps::Status::waiting: return tr(L"Brak aktywnej gry");
    default: return tr(L"Oczekiwanie na klatki gry");
    }
}
}

void Renderer::drawDashboard(float x) {
    const auto& board = context_->dashboard;
    std::erase_if(widget_icons_, [&](const auto& pair) {
        return std::none_of(context_->plugin_widgets.begin(), context_->plugin_widgets.end(), [&](const auto& w) { return w.plugin == pair.first && w.icon; });
    });
    const float left = x + dashboard::margin, inner = friends_layout::drawer_width - 2 * dashboard::margin;
    text(board.time, left - 3, 30, inner - 52, 84, clock_font_.Get(), gray(0.97f));
    text(board.date, left, 111, inner, 30, clock_date_font_.Get(), gray(0.66f));
    const auto organize = dashboard::organizeButton(x);
    const bool toolbar_focus = board.focus == dashboard::reorganize;
    // A round glass button (the drawer is frosted already), white with the focus; while
    // organizing, its check keeps the accent unless focused.
    const auto ink = drawButton(D2D1::RectF(organize.left, organize.top, organize.right, organize.bottom), toolbar_focus ? 1.0f : 0.0f, 1, false);
    const auto glyph = board.editing && !toolbar_focus ? uiAccent(1) : ink;
    const float bx = organize.left + 10, by = organize.top + 10;
    brush(glyph);
    if (board.editing) {
        line(bx + 2, by + 12, bx + 9, by + 19, glyph, 2.4f);
        line(bx + 9, by + 19, bx + 23, by + 4, glyph, 2.4f);
    } else {
        for (const auto r : {D2D1::RectF(bx, by, bx + 9, by + 9), D2D1::RectF(bx + 14, by, bx + 23, by + 9), D2D1::RectF(bx, by + 14, bx + 23, by + 23)})
            target_->DrawRoundedRectangle(D2D1::RoundedRect(r, 2, 2), solid_.Get(), 1.7f);
    }
    target_->PushAxisAlignedClip(D2D1::RectF(x + 12, dashboard::grid_top - 8, x + friends_layout::drawer_width - 10, viewport_.height - 12), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Tiles of every size share one corner (as Metro): the full squircle stretched over a
    // horizontal tile would round it into a pill.
    const float previous_corner = squircle_corner_, previous_reference = corner_reference_;
    squircle_corner_ = 0.4f;
    corner_reference_ = dashboard::tile_height;
    // Focus: one translucent frame gliding between tiles (ViewContext::drawer_glide, drawer-relative
    // x), under the tiles' content; no outline.
    ComPtr<ID2D1Geometry> frame;
    if (const auto& glide = context_->drawer_glide; glide && !toolbar_focus && !board.editing) {
        // Over an icon the frame takes its full squircle (drawDashboardTile).
        if (glide->bottom - glide->top < dashboard::tile_height * 0.75f) squircle_corner_ = 0.5f;
        frame = tileShape(D2D1::RectF(x + glide->left, glide->top, x + glide->right, glide->bottom), tile_shape_);
        squircle_corner_ = 0.4f;
    }
    if (frame) { brush(gray(1, 0.1f)); target_->FillGeometry(frame.Get(), solid_.Get()); }
    for (const auto& item : board.layout.items) drawDashboardTile(item.source, x, !frame);
    squircle_corner_ = previous_corner;
    corner_reference_ = previous_reference;
    target_->PopAxisAlignedClip();
    const float maximum = board.layout.maxScroll(viewport_.height);
    if (maximum > 0) {
        const float track = viewport_.height - dashboard::grid_top - 20;
        const float thumb = std::max(24.0f, track * track / (track + maximum));
        const float y = dashboard::grid_top + (track - thumb) * std::clamp(board.scroll / maximum, 0.0f, 1.0f);
        brush(gray(1, 0.24f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + friends_layout::drawer_width - 7, y, x + friends_layout::drawer_width - 4, y + thumb), 1.5f, 1.5f), solid_.Get());
    }
}

void Renderer::drawDashboardTile(int index, float drawer_left, bool focus_frame) {
    const auto& board = context_->dashboard;
    const auto r = board.layout.tile(index, drawer_left, board.scroll);
    if (r.bottom < dashboard::grid_top - 8 || r.top > viewport_.height - 12) return;
    const float width = r.right - r.left;
    const bool icon = r.bottom - r.top < dashboard::tile_height - 1;
    const bool focused = board.focus == index;
    // Icons are small squares: the full squircle, like an app icon (the larger tiles share one corner).
    const float shared_corner = squircle_corner_;
    if (icon) squircle_corner_ = 0.5f;
    const auto shape = tileShape(D2D1::RectF(r.left, r.top, r.right, r.bottom), tile_shape_);
    squircle_corner_ = shared_corner;
    if (!shape) return;
    // Without the gliding frame the focused tile shows its focus as a lighter fill (no outline).
    brush(focused && board.grabbed ? uiAccent(0.22f) : gray(1, focused && focus_frame ? 0.15f : 0.065f));
    target_->FillGeometry(shape.Get(), solid_.Get());
    if (!board.editing) {
        brush(gray(1, 0.12f)); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f);
    } else {
        const float pulse = 0.5f + 0.5f * std::sin(board.edit_seconds * 4.0f + index * 0.8f);
        brush(uiAccent(focused ? 0.8f + 0.2f * pulse : 0.22f + 0.22f * pulse));
        target_->DrawGeometry(shape.Get(), solid_.Get(), (focused && board.grabbed ? 3.0f : 1.2f) + pulse * 0.5f);
    }
    const float left = r.left + tile_padding, inner = width - 2 * tile_padding;
    if (icon) {
        drawDashboardGlyph(index, (r.left + r.right) / 2 - 15, (r.top + r.bottom) / 2 - 15, gray(0.95f));
        return;
    }
    drawDashboardGlyph(index, left, r.top + tile_padding, gray(0.95f, focused ? 1.0f : 0.84f));
    if (context_->fps_widget && context_->fps_graph && index == dashboard::tile_count + static_cast<int>(context_->plugin_widgets.size()) &&
        context_->game_fps.status == game_fps::Status::ready)
        drawFrameGraph(left + 44, r.top + tile_padding, std::max(40.0f, inner - 44 - 22), 30, context_->game_fps.graph);

    std::wstring title, value, caption;
    auto value_ink = gray(0.97f);
    if (context_->fps_widget && index == dashboard::tile_count + static_cast<int>(context_->plugin_widgets.size())) {
        title = tr(L"FPS gry");
        const auto& reading = context_->game_fps;
        if (reading.status == game_fps::Status::ready) {
            value = fpsNumber(reading.fps) + L" FPS";
            if (context_->fps_low) caption = L"1% LOW  " + fpsNumber(reading.low);
        } else value = fpsStatus(reading.status);
    } else if (index >= dashboard::tile_count) {
        const auto& widget = context_->plugin_widgets[index - dashboard::tile_count];
        title = widget.title; value = widget.text; caption = widget.caption;
    } else switch (static_cast<dashboard::Tile>(index)) {
    case dashboard::Tile::steam:
        title = tr(L"Znajomi Steam");
        if (context_->friends_loading) value = tr(L"Pobieranie ze Steam…");
        else if (!context_->friends || !context_->friends->error.empty()) {
            value = !context_->friends ? std::wstring(tr(L"Brak danych Steam")) :
                    context_->friends->no_presence ? std::wstring(tr(L"Brak statusów od Aurelii")) : context_->friends->error;
        } else {
            const auto& people = context_->friends->friends;
            const auto count = std::count_if(people.begin(), people.end(), online);
            const auto playing = std::count_if(people.begin(), people.end(), [](const SteamFriend& person) { return !person.game.empty(); });
            value = std::to_wstring(count) + tr(L" online");
            if (playing) caption = std::to_wstring(playing) + tr(L" w grze");
            drawTileAvatars(r.right - tile_padding + 4, r.top + tile_padding + 15);
        }
        break;
    case dashboard::Tile::discord:
        title = L"Discord";
        value = board.discord_running ? tr(L"Uruchomiony") : tr(L"Otwórz");
        break;
    case dashboard::Tile::cpu:
    case dashboard::Tile::gpu: {
        const bool cpu = static_cast<dashboard::Tile>(index) == dashboard::Tile::cpu;
        title = cpu ? tr(L"Procesor") : tr(L"Karta graficzna");
        const auto celsius = cpu ? board.sensors.cpu : board.sensors.gpu;
        value = sensors::format(celsius);
        if (board.sensors.sampled && !celsius) caption = tr(L"Brak czujnika");
        if (celsius && *celsius >= hot_celsius) value_ink = palette::danger();
        break;
    }
    case dashboard::Tile::profile: {
        title = board.profile_name.empty() ? std::wstring(tr(L"Profil")) : board.profile_name;
        value = board.profile_value;
        caption = board.profile_caption;
        // A wide tile: this week's seven days as small bars in its top right corner.
        if (width >= dashboard::tile_width * 1.5f) {
            const float bar = 9, gap = 6, chart_h = 46, right = r.right - tile_padding, bottom = r.top + tile_padding + chart_h;
            for (size_t i = 0; i < 7; ++i) {
                const float x = right - (7 - static_cast<float>(i)) * (bar + gap) + gap;
                const float h = std::max(3.0f, chart_h * board.profile_week[i]);
                brush(i == 6 ? uiAccent(0.95f) : gray(1, board.profile_week[i] > 0 ? 0.42f : 0.16f));
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, bottom - h, x + bar, bottom), 3, 3), solid_.Get());
            }
        }
        break;
    }
    }
    // One typography/geometry contract for built-ins, plugins and FPS. Main status
    // never changes colour, font size or vertical origin according to its source. The glyph
    // stays at the top; the text stands on the tile's foot (square tiles are taller than its block).
    const float foot = r.bottom - 160;
    text(title, left, foot + 64, inner, 24, friend_status_font_.Get(), gray(0.70f));
    text(value, left, foot + 89, inner, caption.empty() ? 56.0f : 32.0f, tile_value_font_.Get(), value_ink);
    if (!caption.empty()) text(caption, left, foot + 126, inner, 22, toast_sub_font_.Get(), gray(0.58f));
    // A dot for a widget kept on the game's screen (a click on the tile pins it, another one lets it go).
    const char* pin = context_->fps_widget && index == dashboard::tile_count + static_cast<int>(context_->plugin_widgets.size()) ? "fps" :
        index == static_cast<int>(dashboard::Tile::cpu) ? "cpu" : index == static_cast<int>(dashboard::Tile::gpu) ? "gpu" : nullptr;
    if (pin && std::find(context_->hud_pins.begin(), context_->hud_pins.end(), pin) != context_->hud_pins.end()) {
        brush(uiAccent(0.95f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.right - tile_padding - 5, r.top + tile_padding + 5), 5, 5), solid_.Get());
    }
}

void Renderer::drawFrameGraph(float x, float y, float width, float height, const std::vector<float>& frame_ms) {
    if (frame_ms.size() < 2) return;
    // A fixed scale from the typical frame (2.5x its median, in steps of 5 ms): a spike runs to the top and is
    // clipped there, instead of the whole curve shrinking each time one comes.
    std::vector<float> sorted;
    for (const float value : frame_ms) if (value > 0) sorted.push_back(value);
    if (sorted.empty()) return;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const float median = sorted[sorted.size() / 2];
    const float top = std::clamp(std::ceil(median * 2.5f / 5.0f) * 5.0f, 20.0f, 100.0f);
    brush(gray(1, 0.06f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + width, y + height), 4, 4), solid_.Get());
    const float step = width / static_cast<float>(frame_ms.size() - 1);
    const auto point = [&](size_t i) {
        return D2D1::Point2F(x + step * static_cast<float>(i), y + height - height * std::min(frame_ms[i], top) / top);
    };
    for (size_t i = 1; i < frame_ms.size(); ++i) {
        if (frame_ms[i] <= 0 || frame_ms[i - 1] <= 0) continue;
        brush(std::max(frame_ms[i], frame_ms[i - 1]) > 33.3f ? palette::danger() : uiAccent(0.95f));
        target_->DrawLine(point(i - 1), point(i), solid_.Get(), 1.6f);
    }
}

bool Renderer::paintHud(const ViewContext& context) {
    PAINTSTRUCT ps{}; BeginPaint(hwnd_, &ps);
    bool painted = false;
    if (createTarget()) {
        updateViewport();
        context_ = &context;
        target_->BeginDraw();
        const float scale = std::clamp(context.hud_scale, 0.5f, 1.0f); // AppSettings::hud_size; 1 is the largest.
        target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale * scale, viewport_.scale * scale));
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target_->Clear(D2D1::ColorF(0, 0, 0, 0));
        struct Line { std::wstring label, value; bool hot = false; bool graph = false; std::string id;
                    bool graph_ready = false; std::wstring reserve; }; // graph: the slot is kept; reserve: the widest value, so nothing moves.
        std::vector<Line> lines;
        // A fixed order, whatever order they were switched on in.
        for (const char* id_text : {"fps", "cpu", "gpu", "ram", "vram"}) {
            const std::string id = id_text;
            if (std::find(context.hud_pins.begin(), context.hud_pins.end(), id) == context.hud_pins.end()) continue;
            if (id == "ram") {
                lines.push_back({L"RAM", sensors::formatMemory(context.dashboard.sensors.ram_used, context.dashboard.sensors.ram_total), false, false, id, false, L"88 / 88 GB"});
            } else if (id == "vram") {
                lines.push_back({L"VRAM", sensors::formatMemory(context.dashboard.sensors.vram_used, context.dashboard.sensors.vram_total), false, false, id, false, L"88 / 88 GB"});
            } else if (id == "fps" && context.fps_widget) {
                const auto& reading = context.game_fps;
                Line line{L"FPS", reading.status == game_fps::Status::ready ? fpsNumber(reading.fps) : std::wstring(L"—")};
                if (reading.status == game_fps::Status::ready && context.fps_low && reading.low > 0) line.value += L"   1% " + fpsNumber(reading.low);
                line.graph = context.fps_graph; // The curve appears a few seconds into the game: its place is there from the start.
                line.graph_ready = context.fps_graph && reading.status == game_fps::Status::ready;
                line.reserve = context.fps_low ? L"888   1% 888" : L"888";
                line.id = id;
                lines.push_back(std::move(line));
            } else if (id == "cpu" || id == "gpu") {
                const auto celsius = id == "cpu" ? context.dashboard.sensors.cpu : context.dashboard.sensors.gpu;
                lines.push_back({id == "cpu" ? L"CPU" : L"GPU", sensors::format(celsius), celsius && *celsius >= hot_celsius, false, id, false, L"888 °C"});
            }
        }
        if (!lines.empty() || !context.hud_badges.empty()) {
            const float x = 24 / scale, y = 24 / scale;
            // Chips like the button prompts: a light pill with the label (LIMIT 60, SMOOTH).
            const auto chipWidth = [&](const std::wstring& badge) { return textAdvance(badge, friend_section_font_.Get()) + 18; };
            const auto drawChip = [&](const std::wstring& badge, float left, float center, float width) {
                drawKeyCap(D2D1::RectF(left, center - 11.5f, left + width, center + 11.5f), 1.0f);
                textLine(badge, left, center, width, friend_section_font_.Get(), keyCapInk(1.0f), DWRITE_TEXT_ALIGNMENT_CENTER);
            };
            const auto labelInk = [&](const Line& line) -> D2D1_COLOR_F {
                if (!context.hud_label_colors) return gray(0.70f);
                if (line.id == "fps") return D2D1::ColorF(0.46f, 0.88f, 0.56f);
                if (line.id == "cpu") return D2D1::ColorF(0.46f, 0.71f, 1.0f);
                if (line.id == "gpu") return D2D1::ColorF(0.74f, 0.58f, 1.0f);
                if (line.id == "ram") return D2D1::ColorF(1.0f, 0.75f, 0.38f);
                return D2D1::ColorF(1.0f, 0.56f, 0.76f);
            };
            if (context.hud_layout == 1) {
                // A strip: one line, the items apart by a wide gap, the chips last.
                constexpr float pad = 14, row = 30, gap = 24, label_gap = 8, graph_width = 120, graph_height = 24, chip_gap = 8;
                struct Cell { float label, value; };
                std::vector<Cell> cells;
                float width = 2 * pad;
                for (size_t i = 0; i < lines.size(); ++i) {
                    const auto& line = lines[i];
                    cells.push_back({textAdvance(line.label, friend_status_font_.Get()) + 6, std::max(textAdvance(line.value, friend_status_font_.Get()), textAdvance(line.reserve, friend_status_font_.Get())) + 6});
                    width += cells.back().label + label_gap + cells.back().value + (line.graph ? 10 + graph_width : 0) + (i ? gap : 0);
                }
                for (size_t i = 0; i < context.hud_badges.size(); ++i)
                    width += chipWidth(context.hud_badges[i]) + (i || !lines.empty() ? (i ? chip_gap : gap) : 0);
                brush(D2D1::ColorF(0.04f, 0.045f, 0.05f, 0.62f));
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + width, y + row + 14), 12, 12), solid_.Get());
                const float center = y + (row + 14) * 0.5f;
                float left = x + pad;
                for (size_t i = 0; i < lines.size(); ++i) {
                    const auto& line = lines[i];
                    if (i) left += gap;
                    textLine(line.label, left, center, cells[i].label, friend_status_font_.Get(), labelInk(line)); // Cap-centred on the strip's line, like the chips.
                    left += cells[i].label + label_gap;
                    textLine(line.value, left, center, cells[i].value, friend_status_font_.Get(), line.hot ? palette::danger() : gray(0.97f));
                    left += cells[i].value;
                    if (line.graph) { left += 10; if (line.graph_ready) drawFrameGraph(left, center - graph_height * 0.5f, graph_width, graph_height, context.game_fps.graph); left += graph_width; }
                }
                for (size_t i = 0; i < context.hud_badges.size(); ++i) {
                    if (i || !lines.empty()) left += i ? chip_gap : gap;
                    const float chip = chipWidth(context.hud_badges[i]);
                    drawChip(context.hud_badges[i], left, center, chip);
                    left += chip;
                }
            } else {
                constexpr float width = 250, row = 34, pad = 12;
                constexpr float graph_height = 44, chip_row = 34;
                float height = static_cast<float>(lines.size()) * row + 2 * pad;
                for (const auto& line : lines) if (line.graph) height += graph_height + 6;
                if (!context.hud_badges.empty()) height += chip_row - (lines.empty() ? 0.0f : 4.0f);
                brush(D2D1::ColorF(0.04f, 0.045f, 0.05f, 0.62f));
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + width, y + height), 12, 12), solid_.Get());
                float top = y + pad;
                for (const auto& line : lines) {
                    text(line.label, x + pad + 2, top, 64, row, friend_status_font_.Get(), labelInk(line));
                    text(line.value, x + pad + 66, top, width - 2 * pad - 66, row, friend_status_font_.Get(),
                         line.hot ? palette::danger() : gray(0.97f));
                    top += row;
                    if (line.graph) { if (line.graph_ready) drawFrameGraph(x + pad, top + 2, width - 2 * pad, graph_height, context.game_fps.graph); top += graph_height + 6; }
                }
                float chip_x = x + pad;
                const float chip_cy = top + (lines.empty() ? 0.0f : -4.0f) + chip_row * 0.5f;
                for (const auto& badge : context.hud_badges) {
                    const float chip_width = chipWidth(badge);
                    if (chip_x + chip_width > x + width - pad) break;
                    drawChip(badge, chip_x, chip_cy, chip_width);
                    chip_x += chip_width + 8;
                }
            }
        }
        const auto result = target_->EndDraw();
        context_ = nullptr;
        if (SUCCEEDED(result)) painted = layered_surface_.present(hwnd_);
        else discardTarget();
    }
    EndPaint(hwnd_, &ps);
    return painted;
}

// Up to five online friends as overlapping circles, right-aligned; the first sits on top.
void Renderer::drawTileAvatars(float right, float center_y) {
    if (!context_->friends) return;
    std::vector<const SteamFriend*> shown;
    for (const auto& person : context_->friends->friends)
        if (online(person) && shown.size() < 5) shown.push_back(&person);
    if (shown.empty()) return;
    constexpr float diameter = 28, step = 20, radius = diameter / 2;
    const float first = right - diameter - step * static_cast<float>(shown.size() - 1) + radius;
    for (size_t i = shown.size(); i-- > 0;) {
        const auto center = D2D1::Point2F(first + step * static_cast<float>(i), center_y);
        brush(D2D1::ColorF(0.075f, 0.08f, 0.09f)); // Cut-out ring between overlapping faces.
        target_->FillEllipse(D2D1::Ellipse(center, radius + 2.2f, radius + 2.2f), solid_.Get());
        const auto found = avatars_.find(shown[i]->id);
        if (found != avatars_.end() && found->second.bitmap && found->second.brush) {
            const auto size = found->second.bitmap->GetSize();
            auto* face = found->second.brush.Get();
            face->SetTransform(D2D1::Matrix3x2F::Scale(diameter / size.width, diameter / size.height) *
                               D2D1::Matrix3x2F::Translation(center.x - radius, center.y - radius));
            face->SetOpacity(reveal(found->second));
            target_->FillEllipse(D2D1::Ellipse(center, radius, radius), face);
        } else {
            brush(gray(0.2f));
            target_->FillEllipse(D2D1::Ellipse(center, radius, radius), solid_.Get());
        }
    }
}

// 30x30 line glyphs in the quick bar's style: friends, chat bubble, processor, graphics card.
void Renderer::drawDashboardGlyph(int tile, float x, float y, D2D1_COLOR_F ink) {
    if (context_->fps_widget && tile == dashboard::tile_count + static_cast<int>(context_->plugin_widgets.size())) {
        // A frame-time graph in the same 30x30 line-glyph box as the other built-ins.
        line(x + 2, y + 26, x + 28, y + 26, ink, 2);
        line(x + 2, y + 26, x + 2, y + 3, ink, 2);
        line(x + 5, y + 20, x + 11, y + 13, ink, 2.2f);
        line(x + 11, y + 13, x + 17, y + 18, ink, 2.2f);
        line(x + 17, y + 18, x + 27, y + 6, ink, 2.2f);
        return;
    }
    if (tile >= dashboard::tile_count) {
        const auto& widget = context_->plugin_widgets[tile - dashboard::tile_count];
        if (widget.icon) {
            auto& image = widget_icons_[widget.plugin];
            if (image.pixels != widget.icon) {
                image = {}; image.hero = true; image.short_edge = 128; image.pixels = widget.icon;
                createBitmap(image);
            }
            if (image.bitmap && image.brush) {
                const auto size = image.bitmap->GetSize();
                const float scale = 36 / std::min(size.width, size.height);
                image.brush->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                    D2D1::Matrix3x2F::Translation(x + 15 - size.width * scale / 2, y + 15 - size.height * scale / 2));
                image.brush->SetOpacity(ink.a);
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 15, y + 15), 18, 18), image.brush.Get());
                return;
            }
        }
        const auto monogram = widget.icon_text.empty() ? widget.title.substr(0, 1) : widget.icon_text;
        brush(gray(1, 0.08f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 15, y + 15), 19, 19), solid_.Get());
        // Center monograms (a manifest's icon_text) independently of font metrics.
        ComPtr<IDWriteTextLayout> label;
        if (SUCCEEDED(dwrite_->CreateTextLayout(monogram.c_str(), static_cast<UINT32>(monogram.size()), friend_status_font_.Get(), 38, 38, label.GetAddressOf()))) {
            label->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); label->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            brush(ink); target_->DrawTextLayout(D2D1::Point2F(x - 4, y - 4), label.Get(), solid_.Get());
        }
        return;
    }
    constexpr float stroke = 2.2f;
    const auto path = [&](auto&& build) {
        ComPtr<ID2D1PathGeometry> geometry;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(geometry.GetAddressOf())) || FAILED(geometry->Open(sink.GetAddressOf()))) return;
        build(sink.Get());
        if (SUCCEEDED(sink->Close())) { brush(ink); target_->DrawGeometry(geometry.Get(), solid_.Get(), stroke); }
    };
    const auto arc = [](ID2D1GeometrySink* sink, D2D1_POINT_2F to, float rx, float ry) {
        sink->AddArc(D2D1::ArcSegment(to, D2D1::SizeF(rx, ry), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    };
    brush(ink);
    switch (static_cast<dashboard::Tile>(tile)) {
    case dashboard::Tile::steam:
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 11, y + 10), 5, 5), solid_.Get(), stroke);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 22.5f, y + 8.5f), 4, 4), solid_.Get(), stroke);
        path([&](ID2D1GeometrySink* sink) { // Front shoulders.
            sink->BeginFigure(D2D1::Point2F(x + 1.5f, y + 28), D2D1_FIGURE_BEGIN_HOLLOW);
            arc(sink, D2D1::Point2F(x + 20.5f, y + 28), 9.5f, 9);
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
        });
        path([&](ID2D1GeometrySink* sink) { // The friend behind, peeking out on the right.
            sink->BeginFigure(D2D1::Point2F(x + 22.5f, y + 16.5f), D2D1_FIGURE_BEGIN_HOLLOW);
            arc(sink, D2D1::Point2F(x + 30, y + 24), 7.5f, 7.5f);
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
        });
        break;
    case dashboard::Tile::discord: {
        const float l = x + 2, t = y + 4, r = x + 28, b = y + 22, round = 6;
        path([&](ID2D1GeometrySink* sink) {
            sink->BeginFigure(D2D1::Point2F(l + round, t), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(r - round, t));
            arc(sink, D2D1::Point2F(r, t + round), round, round);
            sink->AddLine(D2D1::Point2F(r, b - round));
            arc(sink, D2D1::Point2F(r - round, b), round, round);
            sink->AddLine(D2D1::Point2F(l + 13, b));
            sink->AddLine(D2D1::Point2F(l + round, b + 6)); // Speech tail.
            sink->AddLine(D2D1::Point2F(l + round, b));
            arc(sink, D2D1::Point2F(l, b - round), round, round);
            sink->AddLine(D2D1::Point2F(l, t + round));
            arc(sink, D2D1::Point2F(l + round, t), round, round);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
        brush(ink);
        for (const float dot : {9.5f, 15.0f, 20.5f})
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + dot, y + 13), 1.8f, 1.8f), solid_.Get());
        break;
    }
    case dashboard::Tile::cpu:
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 7, y + 7, x + 23, y + 23), 3, 3), solid_.Get(), stroke);
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 11.5f, y + 11.5f, x + 18.5f, y + 18.5f), 1.5f, 1.5f), solid_.Get());
        for (const float pin : {11.0f, 15.0f, 19.0f}) {
            line(x + pin, y + 2, x + pin, y + 7, ink, 2);
            line(x + pin, y + 23, x + pin, y + 28, ink, 2);
            line(x + 2, y + pin, x + 7, y + pin, ink, 2);
            line(x + 23, y + pin, x + 28, y + pin, ink, 2);
        }
        break;
    case dashboard::Tile::profile: {
        // The player: a round badge with their initials (as the profile's own picture stands in).
        const auto& board = context_->dashboard;
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 15, y + 15), 14, 14), solid_.Get(), stroke);
        const auto initials = board.profile_initials.empty() ? std::wstring(L"?") : board.profile_initials;
        ComPtr<IDWriteTextLayout> label;
        if (SUCCEEDED(dwrite_->CreateTextLayout(initials.c_str(), static_cast<UINT32>(initials.size()), friend_status_font_.Get(), 30, 30, label.GetAddressOf()))) {
            label->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); label->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            brush(ink); target_->DrawTextLayout(D2D1::Point2F(x, y), label.Get(), solid_.Get());
        }
        break;
    }
    case dashboard::Tile::gpu:
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 1, y + 6, x + 29, y + 22), 3, 3), solid_.Get(), stroke);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 10, y + 14), 4.5f, 4.5f), solid_.Get(), 1.9f);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 10, y + 14), 1.4f, 1.4f), solid_.Get());
        for (const float vent : {10.5f, 14.0f, 17.5f}) line(x + 18, y + vent, x + 25, y + vent, ink, 1.9f);
        line(x + 6, y + 22, x + 6, y + 27, ink, 2); // Edge connector.
        line(x + 6, y + 27, x + 19, y + 27, ink, 2);
        line(x + 19, y + 27, x + 19, y + 22, ink, 2);
        break;
    }
}
