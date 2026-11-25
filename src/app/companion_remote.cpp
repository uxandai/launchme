// Settings → Asystent → "Dostęp z telefonu": her page in a phone's browser (res/remote/index.html)
// over src/net/remote_server.hpp. The same conversation as on this screen (her chat state lives on
// the UI thread, so every chat request is a RemoteMessage there), the character choice and her
// Launchfeed (read from disk on the server's thread). No game is ever started from here.
#include "launcher.hpp"
#include "../platform/embedded_resources.hpp"
#include "../net/companion_characters.hpp"
#include "../net/companion_feed.hpp"
#include "../net/http.hpp"
#include "../net/json.hpp"
#include "../platform/diagnostics.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/user_config.hpp"

#include <algorithm>
#include <cctype>
#include <future>

namespace {
constexpr int page_resource = 65000; // res/remote/index.html (CMakeLists.txt, companion.rc).

std::string q(std::wstring_view text) { return json::quote(text_encoding::utf8Lenient(text)); }
std::string color(const companion::Character& character) {
    char out[8];
    const auto channel = [](float value) { return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
    snprintf(out, sizeof(out), "#%02X%02X%02X", channel(character.r), channel(character.g), channel(character.b));
    return out;
}
std::optional<std::string_view> resource(int id) {
    const auto bytes = embedded_resources::find(id);
    if (!bytes || bytes->empty()) return std::nullopt;
    return std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}
std::string imageType(std::string_view bytes) {
    if (bytes.starts_with("\x89PNG")) return "image/png";
    if (bytes.starts_with("\xFF\xD8")) return "image/jpeg";
    if (bytes.starts_with("GIF8")) return "image/gif";
    if (bytes.size() > 12 && bytes.substr(8, 4) == "WEBP") return "image/webp";
    return "application/octet-stream";
}
remote::Response error(int status, std::string_view why) { return {status, "application/json; charset=utf-8", "{\"error\":" + json::quote(why) + "}"}; }

struct Context {
    HWND hwnd = nullptr;
    std::shared_ptr<std::atomic_bool> alive;
    std::string key;
    std::filesystem::path config;
};

// `work` on the UI thread, its JSON back here; nullopt: the launcher closes or is busy too long.
std::optional<std::string> onUi(const Context& context, const std::atomic_bool& stopping, std::function<std::string(Launcher&)> work) {
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();
    auto message = std::make_unique<RemoteMessage>();
    message->run = [promise, work = std::move(work)](Launcher& app) { promise->set_value(work(app)); };
    if (!context.alive->load() || !PostMessageW(context.hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) return std::nullopt;
    message.release();
    for (int waited = 0; waited < 80; ++waited) { // 8 s
        if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
            try { return future.get(); } catch (...) { return std::nullopt; } // Discarded on shutdown.
        }
        if (stopping || !context.alive->load()) return std::nullopt;
    }
    return std::nullopt;
}

remote::Response serve(const Context& context, const remote::Request& request, const std::atomic_bool& stopping) {
    const auto& path = request.path;
    const bool get = request.method == "GET" || request.method == "HEAD", post = request.method == "POST";
    if (get && (path == "/" || path == "/index.html")) {
        const auto page = resource(page_resource);
        if (!page) return error(404, "page");
        return {200, "text/html; charset=utf-8", std::string(*page)};
    }
    if (get && path == "/manifest.webmanifest") {
        std::string icon = "/api/sprite";
        if (const auto looks = companion::availableLooks(); !looks.empty()) icon += "?look=" + std::string(companion::lookKey(looks.front())) + "&e=smile";
        return {200, "application/manifest+json", "{\"name\":\"Launchme\",\"short_name\":\"Launchme\",\"start_url\":\"/\",\"display\":\"standalone\","
                "\"background_color\":\"#101118\",\"theme_color\":\"#101118\",\"icons\":[{\"src\":" + json::quote(icon) + ",\"sizes\":\"512x512\",\"type\":\"image/png\"}]}"};
    }
    if (get && path == "/sw.js") return {200, "text/javascript", "self.addEventListener('fetch', () => {});"};
    // Her pictures are in the program itself: no key needed.
    if (get && path == "/api/sprite") {
        const auto look = companion::lookFromKey(request.param("look"));
        if (!look) return error(404, "look");
        const auto emotion = companion::emotionFromKey(request.param("e")).value_or(companion::calm);
        const int slot = std::clamp(std::atoi(request.param("c").c_str()), 0, 31);
        for (const auto& [face, outfit] : {std::pair{emotion, slot}, std::pair{companion::calm, slot}, std::pair{emotion, 0}, std::pair{companion::calm, 0}})
            if (const auto bytes = resource(companion_ui::spriteId(*look, companion_ui::Frame::base, face, outfit)))
                return {200, "image/png", std::string(*bytes), "max-age=86400"};
        return error(404, "sprite");
    }
    if (!path.starts_with("/api/")) return error(404, "path");
    if (context.key.empty() || request.param("k") != context.key) return error(403, "key");
    const auto ui = [&](std::function<std::string(Launcher&)> work) {
        const auto answer = onUi(context, stopping, std::move(work));
        return answer ? remote::Response{200, "application/json; charset=utf-8", *answer} : error(503, "busy");
    };
    if (get && path == "/api/state") return ui([](Launcher& app) { return app.remoteState(); });
    if (post && path == "/api/send") {
        auto text = text_encoding::wideLenient(request.body);
        return ui([text = std::move(text)](Launcher& app) mutable { return app.remoteSend(std::move(text)); });
    }
    if (post && path == "/api/look") return ui([key = request.body](Launcher& app) { return app.remoteLook(key); });
    if (post && path == "/api/restart") return ui([](Launcher& app) { return app.remoteRestart(); });
    // Launchfeed: her archive on disk, read here.
    if (get && (path == "/api/feed" || path == "/api/feed/image")) {
        const auto look = companion::lookFromKey(request.param("look"));
        if (!look) return error(404, "look");
        const auto folder = companion::feed::folderOf(context.config, companion::lookKey(*look));
        if (path == "/api/feed/image") {
            const auto id = request.param("id");
            if (id.size() != 64 || id.find_first_not_of("0123456789abcdef") != std::string::npos) return error(404, "id");
            const auto bytes = companion::feed::image(folder, id);
            if (!bytes) return error(404, "image");
            std::string body(bytes->begin(), bytes->end());
            auto type = imageType(body);
            return {200, std::move(type), std::move(body), "max-age=604800"};
        }
        const auto state = companion::feed::load(folder);
        std::string out = "{\"posts\":[";
        size_t count = 0;
        if (state)
            for (const auto& item : state->posts) {
                if (count == 40) break;
                if (count++) out += ',';
                out += "{\"id\":" + json::quote(item.id) + ",\"at\":" + std::to_string(item.at) + ",\"text\":" + q(item.text) +
                       ",\"w\":" + std::to_string(item.width) + ",\"h\":" + std::to_string(item.height) +
                       ",\"liked\":" + (item.social.liked ? "true" : "false") + ",\"comments\":[";
                for (size_t i = 0; i < item.social.comments.size(); ++i) {
                    const auto& comment = item.social.comments[i];
                    out += std::string(i ? "," : "") + "{\"author\":" + q(comment.author) + ",\"text\":" + q(comment.text) +
                           ",\"player\":" + (comment.player ? "true" : "false") + "}";
                }
                out += "]}";
            }
        out += "]}";
        return {200, "application/json; charset=utf-8", std::move(out)};
    }
    return error(404, "path");
}
} // namespace

void Launcher::onRemote(std::unique_ptr<RemoteMessage> message) {
    if (message->run) message->run(*this);
}

void Launcher::companionRemoteSync() {
    // The key is the server's only password: kept encrypted (companion_remote.key), not in
    // settings.ini. A key an older version wrote there moves over once; the next save drops it.
    constexpr const wchar_t* key_file = L"companion_remote.key";
    if (!remote_key_checked_) {
        remote_key_checked_ = true;
        if (!settings_.companion_remote_key.empty()) {
            if (!storedUserSecret(key_file)) saveUserSecret(key_file, text_encoding::wideLenient(settings_.companion_remote_key));
            settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
        } else {
            const auto stored = text_encoding::utf8Lenient(userSecretFile(userConfigDirectory() / key_file));
            if (stored.size() >= 8 && stored.size() <= 64 && std::all_of(stored.begin(), stored.end(), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0; }))
                settings_.companion_remote_key = stored;
        }
    }
    const bool wanted = settings_.companion && settings_.companion_remote;
    const auto port = static_cast<uint16_t>(settings_.companion_remote_port);
    if (!wanted) {
        if (remote_) { remote_->stop(); remote_.reset(); }
        remote_failed_ = false;
        return;
    }
    if (remote_ && remote_->running() && remote_->port() == port) return;
    if (settings_.companion_remote_key.empty()) {
        settings_.companion_remote_key = remote::newKey();
        if (!saveUserSecret(key_file, text_encoding::wideLenient(settings_.companion_remote_key)))
            diagnostics::log("warning", "Phone access: its key could not be saved; a new one comes at the next start");
    }
    if (!remote_) remote_ = std::make_unique<remote::Server>();
    Context context{hwnd_, alive_, settings_.companion_remote_key, userConfigDirectory()};
    remote_failed_ = !remote_->start(port, [context](const remote::Request& request, const std::atomic_bool& stopping) {
        return serve(context, request, stopping);
    }, remote_loopback_);
    if (remote_failed_) diagnostics::log("error", "Phone access: port " + std::to_string(port) + " is taken");
    remote_addresses_ = remote::addresses();
    // The public address, once (only switched on): what a router forwarding the port would expose.
    if (!remote_failed_ && remote_public_.empty() && !remote_loopback_)
        work_.start([hwnd = hwnd_, alive = alive_] {
            const auto response = httpGet(L"https://api.ipify.org", L"", 64);
            auto ip = text_encoding::wideLenient(std::string(response.body.begin(), response.body.end()));
            if (response.status != 200 || ip.empty() || ip.size() > 45 || ip.find_first_not_of(L"0123456789abcdef.:") != std::wstring::npos) return;
            auto message = std::make_unique<RemoteMessage>();
            message->run = [ip = std::move(ip)](Launcher& app) { app.remote_public_ = ip; app.buildSettingsRows(); InvalidateRect(app.hwnd_, nullptr, FALSE); };
            if (alive->load() && PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
}

std::string Launcher::remoteState() {
    if (!settings_.companion) return "{\"off\":true}";
    // As if her chat opened: today's conversation, or she starts one (asleep: it waits for her morning).
    if (!companionOpen() && !companion_.live_chat) {
        companionLoadChat();
        auto& chat = companion_.chat;
        if (chat.log.empty() && !chat.waiting) companionBeginAwake();
        else if (chat.waiting && !companion_.busy) requestCompanion(CompanionMessage::Purpose::chat);
        if (!chat.waiting) companionFlush(); // No chat screen here to reveal the rest of her reply.
    }
    const auto& chat = companion_.live_chat ? *companion_.live_chat : companion_.chat;
    const auto look = companionLook();
    const auto* character = companion::character(look);
    // Her face: thinking while she writes, else how her last message ends.
    auto emotion = companion_.rig.emotion();
    if (chat.waiting) emotion = companion::Emotion::thinking;
    else for (auto list : {&chat.queue, &chat.log}) {
        const auto last = std::find_if(list->rbegin(), list->rend(), [&](const auto& message) { return !message.user && message.speaker == look; });
        if (last != list->rend()) { if (!last->emotions.empty()) emotion = last->emotions.back().emotion; break; }
    }
    std::string out = "{\"en\":" + std::string(i18n::english() ? "true" : "false");
    out += ",\"look\":" + json::quote(companion::lookKey(look)) + ",\"name\":" + q(companion::name(look));
    out += ",\"color\":" + json::quote(character ? color(*character) : "#CCCCCC");
    out += ",\"costume\":" + std::to_string(companionCostume(look)) + ",\"emotion\":" + json::quote(companion::emotionKey(emotion));
    out += ",\"waiting\":" + std::string(chat.waiting ? "true" : "false") + ",\"ended\":" + (chat.ended ? "true" : "false");
    out += ",\"restart\":" + std::string(chat.ended && !companionResting() ? "true" : "false");
    out += ",\"note\":" + q(companion_.live_chat ? std::wstring{} : companionChatNote());
    out += ",\"looks\":[";
    bool first = true;
    for (const auto other : companion::availableLooks()) {
        const auto* each = companion::character(other);
        out += std::string(first ? "" : ",") + "{\"key\":" + json::quote(companion::lookKey(other)) + ",\"name\":" + q(companion::name(other)) +
               ",\"color\":" + json::quote(each ? color(*each) : "#CCCCCC") + ",\"costume\":" + std::to_string(companionCostume(other)) + "}";
        first = false;
    }
    out += "],\"messages\":[";
    std::vector<const companion_ui::Message*> messages;
    for (const auto* list : {&chat.log, &chat.queue})
        for (const auto& message : *list) if (!message.text.empty() && !(message.user && message.free)) messages.push_back(&message);
    const size_t from = messages.size() > 80 ? messages.size() - 80 : 0;
    for (size_t i = from; i < messages.size(); ++i) {
        const auto& message = *messages[i];
        out += std::string(i > from ? "," : "") + "{\"user\":" + (message.user ? "true" : "false") + ",\"text\":" + q(message.text);
        if (!message.user) out += ",\"who\":" + json::quote(companion::lookKey(message.speaker)) + ",\"name\":" + q(companion::name(message.speaker));
        if (message.error) out += ",\"error\":true";
        out += "}";
    }
    return out + "]}";
}

std::string Launcher::remoteSend(std::wstring text) {
    if (!settings_.companion) return "{\"ok\":false,\"why\":\"off\"}";
    if (companion_.live_chat) companionCloseArchive(); // A history page on this screen: back to the live conversation.
    remoteState(); // Today's conversation loaded.
    const auto& chat = companion_.chat;
    const auto trimmed = text.find_first_not_of(L" \t\r\n") == std::wstring::npos;
    if (trimmed || chat.waiting || chat.confirm || chat.mind || chat.feed || chat.ended || chat.archive || companionRouletteSpinning())
        return std::string("{\"ok\":false,\"why\":") + (chat.waiting ? "\"waiting\"" : chat.ended ? "\"ended\"" : "\"busy\"") + "}";
    companionSend(std::move(text));
    return "{\"ok\":true}";
}

std::string Launcher::remoteLook(const std::string& key) {
    const auto look = companion::lookFromKey(key);
    const auto* character = look ? companion::character(*look) : nullptr;
    if (!settings_.companion || !character || character->soon) return "{\"ok\":false}";
    companionChoose(*look);
    buildSettingsRows();
    return "{\"ok\":true}";
}

std::string Launcher::remoteRestart() {
    if (!settings_.companion || !companion_.chat.ended || companion_.chat.archive || companionResting()) return "{\"ok\":false}";
    companionClear();
    return "{\"ok\":true}";
}
