// Widżety → Znajomi Steam → a friend: the conversation inside the drawer, through Aurelia's chat
// commands (history, a live listener, a bounded send). Without Aurelia the friends list hands the
// friend to the Steam client instead (friendsChat). Back returns to the list as it was.
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>

namespace {
using C = aurelia::Command;
using text_encoding::wideLenient;
bool same(const aurelia::ChatMessage& a, const aurelia::ChatMessage& b) { return a.timestamp == b.timestamp && a.self == b.self && a.text == b.text; }
void merge(std::vector<aurelia::ChatMessage>& visible, const std::vector<aurelia::ChatMessage>& messages) {
    for (const auto& m : messages) if (std::none_of(visible.begin(), visible.end(), [&](const auto& old) { return same(old, m); })) visible.push_back(m);
    std::stable_sort(visible.begin(), visible.end(), [](const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
    if (visible.size() > 1000) visible.erase(visible.begin(), visible.begin() + static_cast<std::ptrdiff_t>(visible.size() - 800));
}
}

void Launcher::openFriendChat(uint64_t id, const std::wstring& name, const std::wstring& presence) {
    if (!id || !aurelia_active_ || !friends_open_ || !friends_list_) return;
    closeFriendChat();
    auto& s = friend_chat_;
    s = {}; s.open = true; s.id = id; s.name = name; s.presence = presence;
    s.input = friend_chat_drafts_[id];
    if (!aurelia_authenticated_) { s.failed = true; s.status = tr(L"Zaloguj się w Aurelii: Ustawienia → Biblioteka → Steam"); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    friendChatReconnect();
    audio_.drawer();
}

void Launcher::friendChatReconnect() {
    auto& s = friend_chat_; if (!s.open || !s.id) return;
    friend_chat_cancel_->store(true);
    friend_chat_cancel_ = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++friend_chat_generation_;
    const auto id = s.id;
    s.busy = true; s.live = false; s.failed = false; s.status = tr(L"Wczytywanie rozmowy…");
    aurelia::Request request{C::chat_history}; request.id = id;
    aureliaRequest(request, [generation, id](Launcher& app, aurelia::Reply reply) {
        auto& s = app.friend_chat_;
        if (!s.open || s.id != id || app.friend_chat_generation_ != generation) return;
        s.busy = false;
        if (const auto history = reply.ok ? aurelia::chatHistory(reply.data) : std::nullopt) { merge(s.messages, *history); s.status.clear(); }
        else { s.failed = true; s.status = reply.error.empty() ? tr(L"Nieprawidłowa odpowiedź Aurelii") : reply.error; }
        s.follow = true;
        aurelia::Request live{C::chat_live}; live.id = id;
        app.aureliaRequest(live, [generation, id](Launcher& a, aurelia::Reply r) {
            auto& s = a.friend_chat_;
            if (!s.open || s.id != id || a.friend_chat_generation_ != generation) return;
            s.live = false;
            if (!r.cancelled) { s.failed = true; s.status = r.error.empty() ? tr(L"Rozłączono") : r.error; }
            a.invalidateGameOverlay(); InvalidateRect(a.hwnd_, nullptr, FALSE);
        }, [generation, id](Launcher& a, json::Value event) {
            auto& s = a.friend_chat_;
            if (!s.open || s.id != id || a.friend_chat_generation_ != generation) return;
            const auto kind = aurelia::text(event, "event");
            if (kind == "ready") {
                s.live = true; s.failed = false; s.status.clear();
                // Close the gap between the first history request and arming the listener.
                aurelia::Request history{C::chat_history}; history.id = id;
                a.aureliaRequest(history, [generation, id](Launcher& host, aurelia::Reply reply) {
                    auto& s = host.friend_chat_;
                    if (!s.open || s.id != id || host.friend_chat_generation_ != generation) return;
                    if (const auto messages = reply.ok ? aurelia::chatHistory(reply.data) : std::nullopt) merge(s.messages, *messages);
                    host.invalidateGameOverlay(); InvalidateRect(host.hwnd_, nullptr, FALSE);
                }, {}, a.friend_chat_cancel_);
            } else if (kind == "typing") s.status = tr(L"Pisze…");
            else if (kind == "error") s.status = wideLenient(aurelia::text(event, "message"));
            else if (kind == "message" || kind == "echo") {
                merge(s.messages, {{aurelia::integer(event.get("timestamp")).value_or(0), kind == "echo", wideLenient(aurelia::text(event, "text"))}});
                s.status.clear();
                if (!s.back_focus) s.follow = true;
            }
            a.invalidateGameOverlay(); InvalidateRect(a.hwnd_, nullptr, FALSE);
        }, app.friend_chat_cancel_);
        app.invalidateGameOverlay(); InvalidateRect(app.hwnd_, nullptr, FALSE);
    }, {}, friend_chat_cancel_);
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeFriendChat() {
    auto& s = friend_chat_;
    if (!s.open) return;
    if (s.id) friend_chat_drafts_[s.id] = s.input;
    friend_chat_cancel_->store(true); ++friend_chat_generation_;
    closeKeyboard();
    s = {};
    drawer_glide_.valid = false;
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::friendChatSend() {
    auto& s = friend_chat_;
    if (!s.open || s.input.empty() || s.sending || !aurelia_authenticated_) return;
    const auto message = s.input; const auto generation = friend_chat_generation_; const auto id = s.id;
    s.sending = true;
    aurelia::Request request{C::chat_send}; request.id = id; request.text = message;
    aureliaRequest(request, [generation, id, message](Launcher& app, aurelia::Reply reply) {
        auto& s = app.friend_chat_;
        if (!s.open || s.id != id || app.friend_chat_generation_ != generation) {
            if (reply.ok && aurelia::flag(reply.data, "sent") && app.friend_chat_drafts_[id] == message) app.friend_chat_drafts_.erase(id);
            return;
        }
        s.sending = false;
        if (reply.ok && aurelia::flag(reply.data, "sent")) {
            const auto timestamp = aurelia::integer(reply.data.get("server_timestamp")).value_or(0);
            auto delivered = wideLenient(aurelia::text(reply.data, "modified_message")); if (delivered.empty()) delivered = message;
            merge(s.messages, {{timestamp, true, delivered}});
            if (s.input == message) { s.input.clear(); app.friend_chat_drafts_.erase(id); }
            s.follow = true; s.status.clear();
        } else s.status = reply.error.empty() ? tr(L"Nie wysłano. Tekst zachowano.") : reply.error;
        app.invalidateGameOverlay(); InvalidateRect(app.hwnd_, nullptr, FALSE);
    });
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::friendChatCharacter(wchar_t c) {
    auto& s = friend_chat_;
    if (!friendChatOpen() || c < 32 || c == 127 || s.input.size() >= friend_chat::input_limit) return;
    s.back_focus = false; s.input.push_back(c);
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::friendChatScroll(float amount) {
    auto& s = friend_chat_; if (!s.open) return;
    const auto log = friendsFrame().chat.log();
    const float maximum = std::max(0.0f, s.content - (log.bottom - log.top));
    s.scroll = std::clamp(s.scroll + amount, 0.0f, maximum);
    s.follow = s.scroll >= maximum - 1;
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}

// Keys and the pad (input.cpp, game_overlay.cpp): typing goes to the field; Esc / B is Back to
// the list; Up/Down, PgUp/PgDn and LB/RB scroll; Enter sends; F5 / Y reconnects.
void Launcher::friendChatKey(WPARAM key, bool repeated) {
    auto& s = friend_chat_; if (!friendChatOpen()) return;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (key == VK_ESCAPE) { closeFriendChat(); audio_.navigate(); return; }
    if (key == VK_F5 && !repeated) { friendChatReconnect(); return; }
    if (key == VK_PRIOR || key == VK_NEXT) { friendChatScroll(key == VK_PRIOR ? -320.0f : 320.0f); return; }
    if (key == VK_UP || key == VK_DOWN) {
        // From the field, Up scrolls; at the top of the log it reaches Back.
        if (key == VK_UP && !s.back_focus && s.scroll <= 0.5f) { s.back_focus = true; audio_.navigate(); }
        else if (key == VK_DOWN && s.back_focus) { s.back_focus = false; audio_.navigate(); }
        else friendChatScroll(key == VK_UP ? -80.0f : 80.0f);
        invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (s.back_focus) {
        if ((key == VK_RETURN || key == VK_SPACE || key == VK_BACK) && !repeated) { closeFriendChat(); audio_.navigate(); }
        return;
    }
    if (key == VK_RETURN && !repeated) { friendChatSend(); return; }
    if (key == VK_BACK) {
        if (s.input.empty()) { if (!repeated) { closeFriendChat(); audio_.navigate(); } return; }
        const auto last = s.input.back(); s.input.pop_back();
        if (last >= 0xdc00 && last <= 0xdfff && !s.input.empty() && s.input.back() >= 0xd800 && s.input.back() <= 0xdbff) s.input.pop_back();
    } else if (key == 'V' && control && OpenClipboard(hwnd_)) {
        if (const auto handle = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
                const auto count = std::min<size_t>(GlobalSize(handle) / sizeof(wchar_t), friend_chat::input_limit + 1);
                for (size_t i = 0; i < count && text[i] && s.input.size() < friend_chat::input_limit; ++i) if (text[i] >= 32) s.input.push_back(text[i]);
                GlobalUnlock(handle);
            }
        }
        CloseClipboard();
    }
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::friendChatClick(float x, float y) {
    auto& s = friend_chat_; if (!friendChatOpen()) return;
    auto layout = friendsFrame().chat; // x, y relative to the chat's left edge.
    layout.left = 0;
    switch (layout.hit(x, y)) {
    case 1: closeFriendChat(); audio_.navigate(); return;
    case 2: s.back_focus = false; if (!desktop_mode_ && pad_input_ && !gameOverlayOpen()) openKeyboard(); break;
    case 3: s.back_focus = false; friendChatSend(); break;
    default: break;
    }
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
}
