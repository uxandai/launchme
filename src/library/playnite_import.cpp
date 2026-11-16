#include "playnite_import.hpp"
#include "battlenet.hpp"
#include "emulation_platforms.hpp"
#include "litedb_v7.hpp"
#include "../artwork/cover_cache.hpp"
#include "../model/key_codec.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/profile_transfer.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"
#include "../platform/zip_archive.hpp"
#include "../net/json.hpp"
#include <algorithm>
#include <set>

namespace playnite {
namespace {
// Playnite's built-in library plugins (PlayniteSDK/BuiltInExtensions.cs) and Launchme's store of each.
struct Store { const char* plugin; const char* slug; };
constexpr Store stores[] = {
    {"cb91dfc9-b977-43bf-8e70-55f46e410fab", "steam"},
    {"aebe8b7c-6dc3-4a66-af31-e7375c6b5e9e", "gog"},
    {"00000002-dbd1-46c6-b5d0-b1ba559d10e4", "epic"},
    {"e3c26a3d-d695-4cb7-a769-5ff7612c7edd", "battlenet"},
    {"85dd7072-2f20-4e76-a007-41035e390724", "ea"},
    {"c2f038e5-8b92-4877-91f1-da9094155fc5", "ubisoft"},
    {"7e4fbb5e-2ae3-48d4-8ba0-6b30e7a4e287", "xbox"},
    {"402674cd-4af6-4886-b6ec-0e695bfa0688", "amazon"},
};
constexpr const char* empty_guid = "00000000-0000-0000-0000-000000000000";

std::string lower(std::string text) { for (auto& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return text; }
std::string text(const json::Value& document, const char* name) { const auto* value = document.get(name); return value ? value->stringOr() : std::string{}; }
std::wstring wide(const json::Value& document, const char* name) { return text_encoding::wideLenient(text(document, name)); }
uint64_t number(const json::Value& document, const char* name) {
    const auto* value = document.get(name);
    return value && value->type == json::Value::Type::number && value->numeric > 0 ? static_cast<uint64_t>(value->numeric) : 0;
}
bool flag(const json::Value& document, const char* name) { const auto* value = document.get(name); return value && value->flag; }
std::vector<std::string> ids(const json::Value& document, const char* name) {
    std::vector<std::string> out;
    if (const auto* list = document.get(name); list && list->type == json::Value::Type::array)
        for (const auto& item : list->elements) if (auto id = lower(item.stringOr()); !id.empty()) out.push_back(std::move(id));
    return out;
}
std::map<std::string, std::wstring> names(std::string_view file) {
    std::map<std::string, std::wstring> out;
    for (const auto& document : litedb_v7::documents(file)) {
        const auto id = lower(text(document, "_id"));
        const auto name = wide(document, "Name");
        if (!id.empty() && !name.empty()) out[id] = name;
    }
    return out;
}
// Playnite's own variables in paths ({InstallDir}, {PlayniteDir}) and its backslashes.
std::wstring expand(std::wstring path, const std::wstring& install_dir) {
    for (const auto* variable : {L"{InstallDir}", L"{installdir}"})
        for (size_t at; (at = path.find(variable)) != std::wstring::npos;) path.replace(at, 12, install_dir);
    for (const auto* variable : {L"{PlayniteDir}", L"{EmulatorDir}"}) if (path.find(variable) != std::wstring::npos) return {};
#ifndef _WIN32
    if (path.size() > 2 && path[1] == L':') return path; // A Windows path: the path rules move it.
#endif
    return path;
}
std::filesystem::path picture(const Library& library, const std::wstring& name) {
    if (name.empty() || name.starts_with(L"http://") || name.starts_with(L"https://")) return {};
    std::wstring relative = name;
    for (auto& c : relative) if (c == L'\\') c = L'/';
    std::filesystem::path path(relative);
    if (!path.is_absolute()) path = library.files / path;
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && std::filesystem::file_size(path, ec) <= 20 * 1024 * 1024 ? path : std::filesystem::path{};
}
}

std::filesystem::path findLibrary() {
#ifdef _WIN32
    if (const auto roaming = system_info::environment(L"APPDATA")) {
        const auto library = std::filesystem::path(*roaming) / L"Playnite" / L"library";
        std::error_code ec;
        if (std::filesystem::is_regular_file(library / L"games.db", ec)) return library;
    }
#endif
    return {};
}

std::optional<Library> read(const std::filesystem::path& source, const std::filesystem::path& scratch, std::wstring& error) {
    std::error_code ec;
    std::filesystem::path folder = source;
    std::unique_ptr<zip_archive::Reader> zip;
    if (std::filesystem::is_regular_file(source, ec)) {
        // Playnite's backup: library\games.db … and libraryfiles\<game>\cover.jpg.
        zip = std::make_unique<zip_archive::Reader>();
        if (!zip->open(source)) { error = L"This is not a ZIP file, or it is damaged."; return std::nullopt; }
        std::filesystem::remove_all(scratch, ec);
        folder = scratch / L"library";
        bool found = false;
        for (size_t i = 0; i < zip->entries().size(); ++i) {
            auto name = lower(zip->entries()[i].name);
            for (auto& c : name) if (c == '\\') c = '/';
            if (!name.starts_with("library/") || !name.ends_with(".db") || name.find('/', 8) != std::string::npos) continue;
            const auto file = name.substr(8);
            if (file.find("..") != std::string::npos || file.empty()) continue;
            if (!zip->extract(static_cast<int>(i), folder / text_encoding::wideLenient(file))) { error = L"Cannot unpack the Playnite backup."; return std::nullopt; }
            found |= file == "games.db";
        }
        if (!found) { error = L"This ZIP is no Playnite backup (no library\\games.db)."; return std::nullopt; }
    } else if (!std::filesystem::is_regular_file(folder / L"games.db", ec) && std::filesystem::is_regular_file(folder / L"library" / L"games.db", ec)) {
        folder /= L"library"; // Playnite's own folder (a portable copy).
    }
    if (!std::filesystem::is_regular_file(folder / L"games.db", ec)) { error = L"No games.db here: choose Playnite's library folder or its backup."; return std::nullopt; }

    Library library;
    library.files = folder / L"files";
    const auto games_file = litedb_v7::load(folder / L"games.db", error);
    if (!games_file) return std::nullopt;
    const auto optional = [&](const wchar_t* name) {
        std::wstring ignored;
        return std::filesystem::is_regular_file(folder / name, ec) ? litedb_v7::load(folder / name, ignored) : std::nullopt;
    };
    if (const auto file = optional(L"categories.db")) library.categories = names(*file);
    if (const auto file = optional(L"tags.db")) library.tags = names(*file);
    if (const auto file = optional(L"completionstatuses.db")) library.completion = names(*file);
    if (const auto file = optional(L"platforms.db"))
        for (const auto& document : litedb_v7::documents(*file)) {
            const auto id = lower(text(document, "_id"));
            if (!id.empty()) library.platforms[id] = {wide(document, "Name"), text(document, "SpecificationId")};
        }
    for (const auto& document : litedb_v7::documents(*games_file)) {
        Game game;
        game.id = lower(text(document, "_id"));
        game.name = text(document, "Name");
        if (game.id.empty() || game.name.empty()) continue;
        game.plugin = lower(text(document, "PluginId"));
        game.game_id = text(document, "GameId");
        game.completion = lower(text(document, "CompletionStatusId"));
        game.playtime = number(document, "Playtime");
        game.play_count = number(document, "PlayCount");
        game.last_activity = number(document, "LastActivity");
        game.added = number(document, "Added");
        game.favorite = flag(document, "Favorite");
        game.hidden = flag(document, "Hidden");
        game.installed = flag(document, "IsInstalled");
        game.categories = ids(document, "CategoryIds");
        game.tags = ids(document, "TagIds");
        game.platforms = ids(document, "PlatformIds");
        game.install_dir = wide(document, "InstallDirectory");
        game.cover = wide(document, "CoverImage");
        game.background = wide(document, "BackgroundImage");
        if (const auto* list = document.get("GameActions"); list && list->type == json::Value::Type::array)
            for (const auto& item : list->elements) {
                Action action;
                // An enum: its name ("File") with LiteDB's default mapper, a number with others.
                const auto* type = item.get("Type");
                static constexpr const char* types[] = {"File", "URL", "Emulator", "Script"};
                action.type = !type ? "File" : type->type == json::Value::Type::number ?
                    (type->numeric >= 0 && type->numeric < 4 ? types[static_cast<int>(type->numeric)] : "") : type->stringOr();
                action.path = wide(item, "Path");
                action.arguments = wide(item, "Arguments");
                action.working_dir = wide(item, "WorkingDir");
                action.play = flag(item, "IsPlayAction");
                game.actions.push_back(std::move(action));
            }
        if (const auto* list = document.get("Roms"); list && list->type == json::Value::Type::array)
            for (const auto& item : list->elements) if (auto path = wide(item, "Path"); !path.empty()) game.roms.push_back(std::move(path));
        library.games.push_back(std::move(game));
    }
    if (zip) { // Only the pictures the games name.
        std::map<std::string, std::filesystem::path> wanted;
        for (const auto& game : library.games)
            for (const auto* name : {&game.cover, &game.background}) {
                if (name->empty() || name->find(L':') != std::wstring::npos) continue;
                auto key = lower(text_encoding::utf8Lenient(*name));
                for (auto& c : key) if (c == '\\') c = '/';
                wanted["libraryfiles/" + key] = library.files / std::filesystem::path(text_encoding::wideLenient(key));
            }
        for (size_t i = 0; i < zip->entries().size(); ++i) {
            auto name = lower(zip->entries()[i].name);
            for (auto& c : name) if (c == '\\') c = '/';
            if (const auto found = wanted.find(name); found != wanted.end() && zip_archive::safeName(name)) zip->extract(static_cast<int>(i), found->second);
        }
    }
    if (library.games.empty()) { error = L"Playnite's library holds no games."; return std::nullopt; }
    return library;
}

std::string storeKey(const Game& game) {
    if (game.game_id.empty() || game.plugin.empty() || game.plugin == empty_guid) return {};
    for (const auto& store : stores) {
        if (game.plugin != store.plugin) continue;
        const std::string slug = store.slug;
        if (slug == "steam") return game.game_id.find_first_not_of("0123456789") == std::string::npos ? "steam:" + game.game_id : std::string{};
        if (slug == "battlenet") {
            auto family = battlenet::familyOfCode(game.game_id);
            return "battlenet:" + lower(family.empty() ? game.game_id : family);
        }
        return slug + ':' + game.game_id;
    }
    return {};
}

std::wstring consoleFor(const Platform& platform) {
    static const std::map<std::string, std::wstring_view> specifications{
        {"nintendo_nes", L"NES"}, {"nintendo_famicom", L"NES"}, {"nintendo_super_nes", L"SNES"}, {"nintendo_64", L"N64"},
        {"nintendo_gameboy", L"Game Boy"}, {"nintendo_gameboycolor", L"Game Boy Color"}, {"nintendo_gameboyadvance", L"Game Boy Advance"},
        {"nintendo_gamecube", L"GameCube"}, {"nintendo_ds", L"DS"}, {"nintendo_wii", L"Wii"}, {"nintendo_3ds", L"3DS"},
        {"nintendo_wiiu", L"Wii U"}, {"nintendo_switch", L"Switch"}, {"sony_playstation", L"PS1"}, {"sony_playstation2", L"PS2"},
        {"sony_playstation3", L"PS3"}, {"sony_playstation4", L"PS4"}, {"sony_playstation5", L"PS5"}, {"sony_psp", L"PSP"},
        {"sony_vita", L"PS Vita"}, {"sega_mastersystem", L"Master System"}, {"sega_genesis", L"Mega Drive"}, {"sega_saturn", L"Saturn"},
        {"sega_dreamcast", L"Dreamcast"}, {"xbox", L"Xbox"}, {"xbox360", L"Xbox 360"}, {"commodore_amiga", L"Amiga"},
        {"pc_dos", L"DOS"}, {"arcade", L"Arcade"}, {"mame", L"Arcade"}};
    if (const auto found = specifications.find(platform.specification); found != specifications.end()) return std::wstring(found->second);
    // A platform added by hand: by its name, as Steam ROM Manager's collections are matched.
    const auto name = unicode_text::lower(platform.name);
    for (const auto& console : emulation_platforms) {
        if (unicode_text::lower(console.name) == name) return std::wstring(console.name);
        for (const auto& collection : console.collections) if (!collection.empty() && collection == name) return std::wstring(console.name);
    }
    return {};
}

Plan plan(const Library& library, const Options& options, const Accounts& accounts, const std::vector<custom_games::Entry>& existing) {
    Plan result;
    auto all = existing;
    const auto collection = [&](const std::string& prefix, const std::string& id, const std::wstring& name) {
        const auto key = "local:playnite-" + prefix + '-' + id.substr(0, 8);
        if (!name.empty() && result.state.collections.names.try_emplace(key, name).second) ++result.collections;
        return key;
    };
    for (const auto& game : library.games) {
        std::string key;
        bool steam = false;
        const auto store = storeKey(game);
        if (!store.empty()) { key = store; steam = store.starts_with("steam:"); ++result.store; }
        else if (game.plugin.empty() || game.plugin == empty_guid) {
            // Without a store: a program to start (Gry dodane ręcznie) or a ROM with its console.
            custom_games::Entry entry;
            entry.name = text_encoding::wideLenient(game.name).substr(0, custom_games::max_name);
            const bool emulated = std::any_of(game.actions.begin(), game.actions.end(), [](const Action& a) { return a.type == "Emulator"; }) && !game.roms.empty();
            if (emulated && options.emulated) {
                for (const auto& id : game.platforms)
                    if (const auto found = library.platforms.find(id); found != library.platforms.end())
                        if (entry.platform = consoleFor(found->second); !entry.platform.empty()) break;
                entry.path = expand(game.roms.front(), game.install_dir);
                if (entry.platform.empty()) entry.platform = L"Retro";
            } else if (!emulated && options.own_games) {
                // Its play action that starts a file, else its first such action; links and scripts are not games here.
                const Action* chosen = nullptr;
                for (const auto& action : game.actions)
                    if (action.type == "File" && (!chosen || (action.play && !chosen->play))) chosen = &action;
                if (chosen) {
                    entry.path = expand(chosen->path, game.install_dir);
                    entry.arguments = chosen->arguments.substr(0, custom_games::max_arguments);
                    entry.directory = expand(chosen->working_dir, game.install_dir);
                }
            }
            const auto same = std::find_if(all.begin(), all.end(), [&](const custom_games::Entry& own) {
                return !entry.path.empty() && unicode_text::equalIgnoreCase(own.path.wstring(), entry.path.wstring());
            });
            // A Windows path read on Linux (Playnite runs on Windows) starts nothing here: left out, listed.
            if (!entry.directory.empty() && !entry.directory.is_absolute()) entry.directory.clear();
            if (entry.path.empty() || !entry.path.is_absolute() || all.size() >= custom_games::max_games) { result.skipped.push_back(text_encoding::wideLenient(game.name)); continue; }
            if (same != all.end()) entry.id = same->id;
            else {
                entry.id = custom_games::newId(all);
                if (entry.id.empty()) { result.skipped.push_back(text_encoding::wideLenient(game.name)); continue; }
                all.push_back(entry);
                result.games.push_back(entry);
            }
            if (entry.platform.empty()) ++result.own; else ++result.emulated;
            key = "custom:" + entry.id;
        } else { result.skipped.push_back(text_encoding::wideLenient(game.name)); continue; }

        // The keys Launchme's own history uses for this game (recent_games::historyKey).
        const auto history = steam ? "steam-user:" + std::to_string(accounts.steam) + '/' + key :
            key.starts_with("gog:") && !accounts.gog.empty() ? "gog-user:" + accounts.gog + '/' + key :
            key.starts_with("epic:") && !accounts.epic.empty() ? "epic-user:" + accounts.epic + '/' + key : "local/" + key;
        auto& state = result.state;
        if (options.playtime) {
            if (game.playtime) state.playtime_ms[history] = game.playtime * 1000;
            if (game.last_activity) state.last_played[history] = game.last_activity / 1000;
            if (game.added) state.first_seen[key] = game.added / 1000;
        }
        const auto member = steam ? "steam-user:" + std::to_string(accounts.steam) + '/' + key : key;
        if (options.hidden && game.hidden) state.hidden.insert(member);
        if (options.favorites && game.favorite) {
            if (steam) state.collections.members[std::to_string(accounts.steam) + ":user-collections.favorite"][key] = true;
            else state.collections.members["favorites:local"][key] = true;
        }
        if (options.categories)
            for (const auto& id : game.categories)
                if (const auto found = library.categories.find(id); found != library.categories.end())
                    state.collections.members[collection("c", id, found->second)][member] = true;
        if (options.tags)
            for (const auto& id : game.tags)
                if (const auto found = library.tags.find(id); found != library.tags.end())
                    state.collections.members[collection("t", id, found->second)][member] = true;
        if (options.completion && !game.completion.empty())
            if (const auto found = library.completion.find(game.completion); found != library.completion.end())
                state.collections.members[collection("s", game.completion, found->second)][member] = true;
        if (options.artwork) {
            if (const auto file = picture(library, game.cover); !file.empty()) result.pictures.push_back({key, L"poster", file});
            if (const auto file = picture(library, game.background); !file.empty()) result.pictures.push_back({key, L"hero", file});
        }
    }
    return result;
}

bool stage(const Plan& plan, const std::filesystem::path& config, std::wstring& error) {
    if (profile_transfer::pending(config)) { error = L"Another import waits for Launchme's restart."; return false; }
    const auto folder = config / L"import-pending.new";
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { if (!path.empty()) { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } } cleanup{folder};
    if (!saveLibraryState(plan.state, folder / L"library" / L"library-state.ini")) {
        std::filesystem::create_directories(folder / L"library", ec);
        if (!saveLibraryState(plan.state, folder / L"library" / L"library-state.ini")) { error = L"Cannot write the import."; return false; }
    }
    if (!plan.games.empty() && !custom_games::save(plan.games, folder / L"library" / L"files" / L"custom-games.json")) { error = L"Cannot write the import."; return false; }
    uint32_t id = local_artwork_first_id + 0x100;
    for (const auto& picture : plan.pictures) {
        const auto selection = folder / L"artwork" / L"files" / L"artwork" / L"selections" / text_encoding::wideLenient(key_codec::encode(picture.key));
        std::filesystem::create_directories(selection / picture.kind, ec);
        const auto image = selection / picture.kind / (std::to_wstring(++id) + L".img");
        if (!std::filesystem::copy_file(picture.file, image, std::filesystem::copy_options::overwrite_existing, ec) ||
            !atomic_file::write(selection / (picture.kind + L".selected"), "1 " + std::to_string(id) + '\n')) { error = L"Cannot copy a picture."; return false; }
    }
    auto document = json::object();
    document.properties["mode"] = json::string("merge");
    document.properties["system"] = json::string(
#ifdef _WIN32
        "windows"
#else
        "linux"
#endif
    );
    auto parts = json::array();
    parts.elements.push_back(json::string("library"));
    if (!plan.pictures.empty()) parts.elements.push_back(json::string("artwork"));
    document.properties["parts"] = std::move(parts);
    document.properties["paths"] = json::array();
    document.properties["source"] = json::string("playnite");
    if (!atomic_file::write(folder / L"plan.json", json::serialize(document))) { error = L"Cannot write the import."; return false; }
    if (!atomic_file::moveNew(folder, config / profile_transfer::pending_folder)) { error = L"Cannot prepare the import."; return false; }
    cleanup.path.clear();
    return true;
}
}
