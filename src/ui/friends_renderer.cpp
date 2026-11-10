// Renderer: the right drawer's content (a plugin page, the dashboard or Steam friends) and the
// Steam friends list.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "friends_layout.hpp"
#include "desktop_theme.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

void Renderer::drawFriends() {
    const float progress = smoothFade(context_->friends_progress);
    if (progress < 0.004f) return;
    if (context_->friends_docked) { drawFriendsPage(progress); return; }
    const float width = friends_layout::drawer_width, height = viewport_.height;
    const float x = viewport_.width - width * progress;
    // The drawer has 90% black beneath it; a smooth fade reaches the clear
    // scene one-third of a viewport away from its edge. With glass the scene under the drawer
    // is frosted and the black lets more of it through.
    if (desktop_style_) {
        // The desktop window: a solid side panel, as its other panels, over the barely dimmed window.
        brush(D2D1::ColorF(0.01f, 0.012f, 0.016f, 0.12f * progress));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, height), solid_.Get());
        brush(desktop_theme::current().menu); // Motyw okna.
        target_->FillRectangle(D2D1::RectF(x, 0, viewport_.width, height), solid_.Get());
        brush(D2D1::ColorF(1, 1, 1, 0.08f));
        target_->FillRectangle(D2D1::RectF(x, 0, x + 1, height), solid_.Get());
    } else if (const bool frosted = frostPane(D2D1::RectF(x, 0, viewport_.width, height), progress); friends_backdrop_) {
        const float edge = viewport_.width - width;
        friends_backdrop_->SetStartPoint(D2D1::Point2F(edge - viewport_.width * drawer_style_.fade_fraction, 0));
        friends_backdrop_->SetEndPoint(D2D1::Point2F(edge, 0));
        friends_backdrop_->SetOpacity(progress * drawer_style_.opacity * (frosted ? 1.0f - 0.3f * glass_level_ : 1.0f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, height), friends_backdrop_.Get());
    }
    if (context_->plugin_drawer) drawPluginPage(x, true);
    else if (context_->dashboard.list && context_->friend_chat) drawFriendChat(x);
    else if (context_->dashboard.list) drawFriendsList(x);
    else drawDashboard(x);
}

// A friend's conversation (friend_chat_view.hpp): Back and the name as the list's header, the
// messages as bubbles (theirs on the left, yours on the right) and the field with its Send button.
// The desktop window's Znajomi page (ViewContext::friends_docked): in the library's place beside the
// sidebar, the list on the left and the conversation (or what to do) beside it, in the window's theme.
void Renderer::drawFriendsPage(float progress) {
    const float dock_w = context_->friends_dock_right - context_->friends_dock_left, dock_h = context_->friends_dock_bottom - context_->friends_dock_top;
    if (dock_w <= 0 || dock_h <= 0) return;
    // As the docked Profil: the window's scale (a little smaller), at the dock's corner.
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    const auto saved = viewport_;
    const auto desk = desktopViewport();
    constexpr float compact = friends_layout::docked_scale;
    const float w = dock_w / compact, h = dock_h / compact;
    viewport_.scale = desk.scale * compact; viewport_.width = w; viewport_.height = h;
    target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale) *
                          D2D1::Matrix3x2F::Translation(context_->friends_dock_left * desk.scale, context_->friends_dock_top * desk.scale));
    target_->PushAxisAlignedClip(D2D1::RectF(0, 0, w, h), D2D1_ANTIALIAS_MODE_ALIASED);
    const auto bg = desktop_theme::current().app;
    brush(D2D1::ColorF(bg.r, bg.g, bg.b, std::min(1.0f, progress * 1.6f)));
    target_->FillRectangle(D2D1::RectF(0, 0, w, h), solid_.Get());
    const float list = friends_layout::drawer_width;
    const auto panel = desktop_theme::current().side;
    brush(D2D1::ColorF(panel.r, panel.g, panel.b, progress));
    target_->FillRectangle(D2D1::RectF(0, 0, list, h), solid_.Get());
    brush(D2D1::ColorF(1, 1, 1, 0.07f * progress));
    target_->FillRectangle(D2D1::RectF(list, 0, list + 1, h), solid_.Get());
    drawFriendsList(0);
    if (context_->friend_chat) drawFriendChat(list, std::max(360.0f, w - list));
    else {
        const bool chat = context_->friends_chat_here;
        textLine(chat ? tr(L"Wybierz znajomego, aby porozmawiać") : tr(L"Wybierz znajomego, aby otworzyć czat w kliencie Steam"),
                 list + 40, h * 0.45f, w - list - 80, desktop_title_font_ ? desktop_title_font_.Get() : category_font_.Get(), palette::muted(0.9f * progress),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        if (!chat) textLine(tr(L"Czat w Launchme działa z Aurelią: Ustawienia → Biblioteka → Steam."), list + 40, h * 0.45f + 36, w - list - 80,
                            friend_status_font_.Get(), palette::muted(0.6f * progress), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    target_->PopAxisAlignedClip();
    viewport_ = saved;
    target_->SetTransform(base);
}

// Profil's Back (profile_renderer.cpp): a glass square with an arrow.
void Renderer::pageBack(D2D1_RECT_F back, float focus, bool hovered) {
    drawButtonGlass(back, 1, false, hovered);
    drawButtonFocus(back, focus);
    const auto ink = buttonInk(focus);
    const float cx = (back.left + back.right) * 0.5f, cy = (back.top + back.bottom) * 0.5f;
    line(cx - 7, cy, cx + 7, cy, ink, 2);
    line(cx - 7, cy, cx - 1, cy - 6, ink, 2);
    line(cx - 7, cy, cx - 1, cy + 6, ink, 2);
}

void Renderer::drawFriendChat(float x, float width) {
    auto& s = *context_->friend_chat;
    const friend_chat::Layout layout{x, viewport_.height, width, context_->friends_docked};
    const auto back = layout.back();
    const auto back_rect = D2D1::RectF(back.left, back.top, back.right, back.bottom);
    if (!layout.page) { // The page needs none: the list stays beside the conversation.
        brush(drawButton(back_rect, s.back_focus ? 1.0f : 0.0f, 1, false, s.hover == 1));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f), 3, 3), solid_.Get());
    }
    const float title_left = layout.page ? x + 26 : back.right + 14, title_width = x + layout.width - 24 - title_left;
    textLine(s.name, title_left, (back.top + back.bottom) * 0.5f, title_width, category_font_.Get(), color(0.95f, 0.95f, 0.95f));
    // One quiet line under it: what is happening, else what the friend is doing.
    const bool problem = s.failed && !s.status.empty();
    textLine(s.status.empty() ? s.presence : s.status, x + 26, 94, layout.width - 50, friend_status_font_.Get(),
             problem ? palette::danger() : color(0.62f, 0.62f, 0.62f));
    // Messages, measured first: the newest stay in view while the list follows them.
    const auto log = layout.log();
    const float log_width = log.right - log.left, bubble_max = std::floor(log_width * 0.82f), pad_x = 14, pad_y = 10, gap = 8;
    struct Measured { float width = 0, height = 0; };
    std::vector<Measured> sizes(s.messages.size());
    float content = 0;
    for (size_t i = 0; i < s.messages.size(); ++i) {
        const auto& text = s.messages[i].text;
        const float inner = bubble_max - 2 * pad_x;
        const float h = paragraph(text, 0, 0, inner, friend_name_font_.Get(), palette::text(), 200, false);
        const float w = std::min(inner, std::ceil(textAdvance(text, friend_name_font_.Get())) + 2);
        sizes[i] = {w + 2 * pad_x, std::max(h, typeMetrics(friend_name_font_.Get()).line) + 2 * pad_y};
        content += sizes[i].height + gap;
    }
    s.content = content;
    const float visible = log.bottom - log.top, maximum = std::max(0.0f, content - visible);
    s.scroll = s.follow ? maximum : std::clamp(s.scroll, 0.0f, maximum);
    target_->PushAxisAlignedClip(D2D1::RectF(log.left - 2, log.top, log.right + 2, log.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Fewer messages than the log holds sit at its bottom, next to the field.
    float y = log.top - s.scroll + std::max(0.0f, visible - content);
    for (size_t i = 0; i < s.messages.size(); ++i) {
        const auto& message = s.messages[i];
        const auto size = sizes[i];
        if (y + size.height >= log.top && y <= log.bottom) {
            const float left = message.self ? log.right - size.width : log.left;
            const auto box = D2D1::RectF(left, y, left + size.width, y + size.height);
            brush(message.self ? color(1, 1, 1, 0.16f) : color(1, 1, 1, 0.07f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(box, 16, 16), solid_.Get());
            paragraph(message.text, left + pad_x, y + pad_y, size.width - 2 * pad_x, friend_name_font_.Get(), palette::text(), 200);
        }
        y += size.height + gap;
    }
    if (s.messages.empty())
        textLine(s.busy ? tr(L"Wczytywanie rozmowy…") : tr(L"Brak wiadomości"), log.left + 2, log.bottom - 24, log_width, friend_status_font_.Get(), color(0.55f, 0.55f, 0.55f));
    target_->PopAxisAlignedClip();
    if (maximum > 0) {
        const float thumb = std::max(24.0f, visible * visible / content);
        const float top = log.top + (visible - thumb) * (s.scroll / maximum);
        brush(color(1, 1, 1, 0.22f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(log.right + 6, top, log.right + 9, top + thumb), 1.5f, 1.5f), solid_.Get());
    }
    // The field: a pill, brighter and outlined in the accent while it takes typing.
    const auto f = layout.field();
    const auto field = D2D1::RectF(f.left, f.top, f.right, f.bottom);
    const bool typing = !s.back_focus;
    brush(color(1, 1, 1, typing ? 0.12f : s.hover == 2 ? 0.08f : 0.06f)); target_->FillRoundedRectangle(pill(field), solid_.Get());
    if (typing) { brush(uiAccent()); target_->DrawRoundedRectangle(pill(field), solid_.Get(), 1.5f); }
    const float middle = (field.top + field.bottom) * 0.5f, text_left = field.left + 20, room = field.right - field.left - 40;
    const auto shown = s.input.size() > 80 ? L"…" + s.input.substr(s.input.size() - 79) : s.input;
    if (shown.empty()) textLine(tr(L"Wiadomość"), text_left, middle, room, friend_name_font_.Get(), color(0.5f, 0.5f, 0.5f));
    else textLine(shown, text_left, middle, room, friend_name_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_LEADING);
    if (typing && std::sin(elapsed_ * 4.0f) >= 0)
        textLine(L"│", text_left + std::min(room - 6, shown.empty() ? 0.0f : textAdvance(shown, friend_name_font_.Get())) - 2, middle, 20, friend_name_font_.Get(), palette::text());
    // Send: a round button with an arrow, faint while there is nothing to send.
    const auto send_box = layout.send();
    const auto send = D2D1::RectF(send_box.left, send_box.top, send_box.right, send_box.bottom);
    const bool ready = !s.input.empty() && !s.sending;
    const auto ink = drawButton(send, 0, 1, false, s.hover == 3 && ready, ready);
    const float cx = (send.left + send.right) * 0.5f, cy = (send.top + send.bottom) * 0.5f;
    line(cx - 7, cy, cx + 7, cy, ink, 2);
    line(cx + 1, cy - 6, cx + 7, cy, ink, 2);
    line(cx + 1, cy + 6, cx + 7, cy, ink, 2);
}

// Steam friends list inside the drawer, opened from the dashboard's Steam friends tile.
void Renderer::drawFriendsList(float x) {
    const float width = friends_layout::drawer_width, height = viewport_.height;
    // Back button: same shape as the grid screens' one; B/Esc do the same.
    const bool page = context_->friends_docked;
    const auto back = friends_layout::listBack(x, page);
    // Focus: one frame gliding between Back and the rows (ViewContext::drawer_glide, drawer-relative x).
    const auto& glide = context_->drawer_glide;
    const auto highlight = [&](float alpha) {
        if (!glide) return;
        brush(color(0.88f, 0.88f, 0.88f, alpha));
        const auto r = D2D1::RectF(x + glide->left, glide->top, x + glide->right, glide->bottom);
        target_->FillRoundedRectangle(r.bottom - r.top <= 64 ? pill(r) : D2D1::RoundedRect(r, 16, 16), solid_.Get());
    };
    highlight(0.13f);
    // Back: a glass circle with the dot; the focus frame gliding onto it marks it as well.
    const bool back_focus = context_->friends_back || (context_->friends && context_->friends->friends.empty());
    if (page) { // The docked Profil's Back and title.
        pageBack(D2D1::RectF(back.left, back.top, back.right, back.bottom), back_focus ? 1.0f : 0.0f, false);
        textLine(tr(L"Znajomi"), back.right + 18, (back.top + back.bottom) * 0.5f, width - (back.right - x) - 40,
                 createProfileFonts() ? profile_title_font_.Get() : category_font_.Get(), palette::text());
    } else {
    brush(drawButton(D2D1::RectF(back.left, back.top, back.right, back.bottom), back_focus ? 1.0f : 0.0f, 1, false));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f), 3, 3), solid_.Get());
    textLine(tr(L"ZNAJOMI"), back.right + 14, (back.top + back.bottom) * 0.5f, width - (back.right - x) - 40, category_font_.Get(), color(0.93f, 0.93f, 0.93f));
    }
    if (context_->friends_loading) {
        text(tr(L"Pobieranie ze Steam…"), x + 26, 121, width - 50, 34,
             toast_sub_font_.Get(), color(0.71f, 0.71f, 0.71f));
        return;
    }
    if (!context_->friends || !context_->friends->error.empty()) {
        const auto value = context_->friends ? context_->friends->error : std::wstring(tr(L"Brak danych Steam"));
        text(value, x + 26, 122, width - 50, 78, toast_sub_font_.Get(),
             color(0.78f, 0.78f, 0.78f));
        return;
    }
    const auto& people = context_->friends->friends;
    if (people.empty()) {
        text(tr(L"Lista znajomych jest pusta"), x + 26, 122, width - 50, 36,
             toast_sub_font_.Get(), color(0.71f, 0.71f, 0.71f));
        return;
    }
    target_->PushAxisAlignedClip(D2D1::RectF(x + 1, 104, viewport_.width, height - 12),
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float y = 115.0f - context_->friends_scroll_glide.value_or(static_cast<float>(context_->friends_scroll));
    constexpr const wchar_t* headings[] = {trKey(L"W GRZE"), trKey(L"DOSTĘPNI"), trKey(L"NIEDOSTĘPNI")};
    for (int group = 0; group < 3; ++group) {
        const auto matches = [group](const SteamFriend& person) {
            return group == 0 ? !person.game.empty() : group == 1 ? person.game.empty() && person.status > 0 :
                   person.game.empty() && person.status == 0;
        };
        const size_t count = static_cast<size_t>(std::count_if(people.begin(), people.end(), matches));
        if (!count) continue;
        if (y >= 95 && y <= height)
            text(std::wstring(tr(headings[group])) + L"  " + std::to_wstring(count), x + 30, y + 5, width - 55, 33,
                 friend_section_font_.Get(), color(0.78f, 0.78f, 0.78f));
        y += friends_layout::section_height;
        for (size_t index = 0; index < people.size(); ++index) {
            const auto& person = people[index];
            if (!matches(person)) continue;
            if (y + friends_layout::row_height >= 104 && y < height - 8) {
                if (!glide && !context_->friends_back && index == context_->friends_cursor) {
                    brush(color(0.88f, 0.88f, 0.88f, 0.13f));
                    const auto row = D2D1::RectF(x + 18, y + 5, x + width - 17, y + friends_layout::row_height - 5);
                    target_->FillRoundedRectangle(row.bottom - row.top <= 64 ? pill(row) : D2D1::RoundedRect(row, 16, 16), solid_.Get());
                }
                const float ax = x + 30, ay = y + 14;
                const auto found = avatars_.find(person.id);
                if (found != avatars_.end() && found->second.bitmap) {
                    target_->DrawBitmap(found->second.bitmap.Get(), D2D1::RectF(ax, ay, ax + 52, ay + 52));
                } else {
                    brush(color(0.133f, 0.133f, 0.133f)); // #222
                    target_->FillRoundedRectangle(D2D1::RoundedRect(
                        D2D1::RectF(ax, ay, ax + 52, ay + 52), 9, 9), solid_.Get());
                }
                // Name and status as one group on the avatar's centre line.
                const float shade = group == 0 ? 0.79f : 0.62f;
                textGroup(person.name, person.game.empty() ? std::wstring(person.status ? tr(L"Dostępny") : tr(L"Niedostępny")) : person.game,
                          x + 101, ay + 26, width - 122, friend_name_font_.Get(), color(0.95f, 0.95f, 0.95f),
                          friend_status_font_.Get(), color(shade, shade, shade), 1);
            }
            y += friends_layout::row_height;
        }
    }
    target_->PopAxisAlignedClip();
}
