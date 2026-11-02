#include "aurelia.hpp"
#include "../platform/atomic_file.hpp"
#include <fstream>

namespace aurelia {
Reply checked(const Request& request, Reply reply) {
    if (!reply.ok) return reply;
    const char* status = nullptr;
    switch (request.command) {
    case Command::install: case Command::install_resume: status = "installed"; break;
    case Command::relink: status = "relinked"; break;
    case Command::import_files: status = "imported"; break;
    case Command::update: status = "updated"; break;
    case Command::verify: status = "verified"; break;
    case Command::move: status = "moved"; break;
    case Command::downgrade: status = "downgraded"; break;
    case Command::play: status = "finished"; break;
    default: break;
    }
    if (status && (text(reply.data, "status") != status || integer(reply.data.get("app_id")) != request.app)) {
        reply.ok = false; reply.error = tr(L"Nieprawidłowa odpowiedź Aurelii");
    }
    return reply;
}

Reply launchStandalone(const Transport& invoke, Request request, const Cancel& cancel, const Event& event) {
    if (!invoke || !request.app) return {false, false, {}, tr(L"Nieprawidłowe polecenie Aurelii")};
    const auto stopped = [&] { return cancel && cancel(); };
    json::Value saves; bool finished = false;
    const auto result = [&](Reply reply) {
        auto data = json::object(); data.properties["launch_finished"] = json::boolean(finished);
        if (saves.type != json::Value::Type::null_value) data.properties["cloud_result"] = saves;
        reply.data = std::move(data); return reply;
    };
    // The game cannot run without the Steam client here (preflight): the caller may use the client.
    const auto refuse = [&](const wchar_t* message) {
        auto reply = result({false, false, {}, message}); reply.data.properties["steam_required"] = json::boolean(true); return reply;
    };
    const auto send = [&](Request r) {
        if (stopped()) return Reply{false, true, {}, tr(L"Anulowano")};
        return checked(r, invoke(r, cancel, event));
    };
    // Upstream forces Steam for unowned/Family-Shared games even without --steam. Refuse that
    // path explicitly rather than silently violating the user's "Launch without Steam" choice.
    auto owned = send({Command::library});
    if (!owned.ok) return result(std::move(owned));
    const json::Value* game = nullptr;
    if (owned.data.type == json::Value::Type::array) for (const auto& item : owned.data.elements)
        if (integer(item.get("app_id")) == request.app) { if (game) return result({false, false, {}, tr(L"Nieprawidłowa odpowiedź Aurelii")}); game = &item; }
    if (!game || !flag(*game, "is_owned") || flag(*game, "is_family_shared") || flag(*game, "from_windows_steam"))
        return refuse(tr(L"Ta gra wymaga klienta Steam lub potwierdzenia własności."));
    auto targets = send({Command::launch_targets, request.app});
    if (!targets.ok) return result(std::move(targets));
    bool standalone = false;
#ifndef _WIN32
    bool native = false; // Linux: the game's native build when it is installed, else the Windows one through Proton.
#endif
    if (const auto* choices = targets.data.get("targets"); choices && choices->type == json::Value::Type::array)
        for (const auto& target : choices->elements) {
            if (text(target, "platform") == "windows" && text(target, "steam_mode") == "none" && flag(target, "available")) standalone = true;
#ifndef _WIN32
            if (text(target, "platform") == "linux" && flag(target, "available")) native = true;
#endif
        }
#ifndef _WIN32
    request.text = native ? L"linux" : L"windows";
    standalone = standalone || native;
#endif
    if (!standalone) return refuse(tr(L"Aurelia nie udostępnia zainstalowanej wersji Windows bez Steam dla tej gry."));
    const auto sync = [&](Request::Direction direction) {
        Request r{Command::cloud_sync, request.app}; r.direction = direction;
        auto reply = send(r); saves = reply.ok ? reply.data : json::object();
        if (!reply.ok) { saves.properties["status"] = json::string("failed"); return reply; }
        const auto state = cloud(reply, true);
        if (state.state != CloudState::synced) { reply.ok = false; reply.error = cloudLabel(state.state); }
        return reply;
    };
    if (request.auto_cloud) {
        if (event) { auto e = json::object(); e.properties["event"] = json::string("launchme-cloud-down"); event(e); }
        auto before = sync(Request::Direction::down);
        if (!before.ok) return result(std::move(before)); // No partial/conflicted restore may proceed to play.
    }
    if (stopped()) return result({false, true, {}, tr(L"Anulowano")});
    if (event) { auto e = json::object(); e.properties["event"] = json::string("launchme-starting"); event(e); }
    request.command = Command::play; request.steam = false;
    auto played = send(request);
    if (!played.ok) return result(std::move(played));
    finished = true;
    if (request.auto_cloud) {
        if (event) { auto e = json::object(); e.properties["event"] = json::string("launchme-cloud-up"); event(e); }
        return result(sync(Request::Direction::up));
    }
    return result(std::move(played));
}

bool saveLibraryCache(const std::filesystem::path& folder, uint64_t account, const json::Value& games) {
    if (!folder.is_absolute() || !library(games, account)) return false;
    auto value = json::object(); value.properties["account"] = json::string(std::to_string(account)); value.properties["games"] = games;
    return atomic_file::write(folder / (std::to_wstring(account) + L".json"), json::serialize(value));
}
std::optional<json::Value> loadLibraryCache(const std::filesystem::path& folder, uint64_t account) {
    if (!folder.is_absolute() || !account) return {};
    std::ifstream file(folder / (std::to_wstring(account) + L".json"), std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 16 * 1024 * 1024) return {};
    std::string bytes(static_cast<size_t>(file.tellg()), '\0'); file.seekg(0);
    if (!file.read(bytes.data(), bytes.size())) return {};
    auto parsed = json::parse(bytes, 16 * 1024 * 1024);
    if (!parsed || integer(parsed->get("account")) != account) return {};
    const auto* games = parsed->get("games");
    if (!games || !library(*games, account)) return {};
    return *games;
}
}
