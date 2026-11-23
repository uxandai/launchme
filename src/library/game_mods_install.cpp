#include "game_mods_install.hpp"
#include "game_mods_sources.hpp"
#include "../i18n/i18n.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/hashing.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"
#include "../platform/zip_archive.hpp"

#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

namespace game_mods {
namespace {
using sources::Download;

std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }
std::string body(const HttpResponse& response) { return std::string(response.body.begin(), response.body.end()); }

// One install or removal: its request, services, the folder's record and what the folder holds now.
struct Job {
    const Request& request;
    const fs::path& data;
    const Services& services;
    Record record;
    State state;
    std::wstring error;

    fs::path folder() const { return request.target.folder; }
    fs::path cache(std::string_view part) const { return data / "cache" / fs::path(wide(part)); }
    void progress(const std::wstring& text) const { if (services.progress) services.progress(text); }
    bool cancelled() const { return services.cancelled && services.cancelled(); }
    bool fail(std::wstring text) { if (error.empty()) error = std::move(text); return false; }
    void rescan() { state = scan(request.target, &record); }
    RecordEntry& entry(Mod mod) { return record.mods[slug(mod)]; }
    bool save() { return saveRecord(data, record) || fail(tr(L"Nie udało się zapisać listy zainstalowanych modów.")); }

    std::optional<std::string> text(const std::wstring& url) {
        if (!services.fetch) { fail(tr(L"Brak połączenia z siecią.")); return std::nullopt; }
        const auto response = services.fetch(url);
        if (response.status != 200 || response.truncated) {
            fail(std::wstring(tr(L"Nie udało się pobrać listy wydań")) + L" (" + (response.status ? std::to_wstring(response.status) : std::wstring(L"brak sieci")) + L")");
            return std::nullopt;
        }
        return body(response);
    }
    // A download kept in the cache: fetched once per version.
    bool file(const std::wstring& url, const fs::path& target, const std::wstring& what) {
        if (file_io::isFile(target)) { std::error_code ec; if (fs::file_size(target, ec) > 0 && !ec) return true; }
        if (cancelled()) return fail(tr(L"Przerwano."));
        progress(std::wstring(tr(L"Pobieranie: ")) + what + L"…");
        if (!services.download || !services.download(url, target)) return fail(std::wstring(tr(L"Nie udało się pobrać: ")) + what);
        return true;
    }
    // What the folder's file `name` is, when it is ReShade (Launchme's or another tool's).
    bool reshadeAt(const fs::path& file) const {
        if (!file_io::isFile(file)) return false;
        if (const auto* own = record.find(Mod::reshade))
            for (const auto& placed : own->files) if (placed.path == file) return true;
        return isReShade(file);
    }
    std::wstring reshadeSlot() const { return state.x86 ? L"ReShade32.dll" : L"ReShade64.dll"; }
    // OptiScaler (or ANR's) needs `slot`: a ReShade there moves to ReShade64.dll, which OptiScaler loads.
    bool freeSlot(const std::wstring& slot, RecordEntry& taker) {
        const auto file = folder() / slot;
        if (!reshadeAt(file)) return true;
        const auto moved = folder() / reshadeSlot();
        std::error_code ec;
        if (fs::exists(moved, ec)) return fail(std::wstring(tr(L"W folderze gry jest już ")) + moved.filename().wstring());
        auto& reshade = entry(Mod::reshade);
        const auto own = std::find_if(reshade.files.begin(), reshade.files.end(), [&](const PlacedFile& placed) { return placed.path == file; });
        fs::rename(file, moved, ec);
        if (ec) return fail(std::wstring(tr(L"Nie udało się przenieść ReShade: ")) + file.filename().wstring());
        if (own != reshade.files.end()) {
            // Launchme's ReShade: its record follows the file; the slot's original backup is the taker's now.
            taker.files.push_back({file, own->backup});
            own->path = moved;
            own->backup.clear();
        } else {
            if (reshade.files.empty() && reshade.method.empty()) record.mods.erase(slug(Mod::reshade));
            taker.moved = utf8(slot);
        }
        return true;
    }
    // After OptiScaler (or ANR's) went: ReShade back into its own slot when it is free.
    void restoreSlot(const RecordEntry& taker) {
        const auto moved = folder() / reshadeSlot();
        if (!reshadeAt(moved)) return;
        std::error_code ec;
        if (!taker.moved.empty()) {
            const auto back = folder() / wide(taker.moved);
            if (!fs::exists(back, ec)) fs::rename(moved, back, ec);
            return;
        }
        auto found = record.mods.find(slug(Mod::reshade));
        if (found == record.mods.end()) return;
        auto& reshade = found->second;
        const auto own = std::find_if(reshade.files.begin(), reshade.files.end(), [&](const PlacedFile& placed) { return placed.path == moved; });
        if (own == reshade.files.end()) return;
        const auto back = folder() / wide(methodOr(Mod::reshade, reshade.method));
        if (fs::exists(back, ec)) return; // The slot's original came back: ReShade stays ReShade64.dll (unused).
        fs::rename(moved, back, ec);
        if (!ec) own->path = back;
    }
    // OptiScaler.ini's LoadReshade: on while a ReShade sits beside it as ReShade64.dll. Launchme's own
    // OptiScaler keeps the file's original as its backup; another tool's (or ANR's) is edited in place.
    void loadReshade(bool on) {
        const auto ini = folder() / L"OptiScaler.ini";
        if (!file_io::isFile(ini)) return;
        const auto text = file_io::readText(ini, 4 * 1024 * 1024);
        const auto value = on ? "true" : "false";
        if (iniValue(text, "Plugins", "LoadReshade") == value) return;
        const auto updated = setIniValue(text, "Plugins", "LoadReshade", value);
        if (record.mods.contains(slug(Mod::optiscaler))) placeBytes(updated, ini, entry(Mod::optiscaler));
        else atomic_file::write(ini, updated);
    }
    // Takes Launchme's OptiScaler out (an update or another file name) and puts ReShade back.
    bool takeOut(Mod mod) {
        auto found = record.mods.find(slug(mod));
        if (found == record.mods.end()) return true;
        auto taker = found->second;
        if (!removeFiles(found->second)) { save(); return fail(tr(L"Nie udało się usunąć wszystkich plików (czy gra działa?).")); }
        record.mods.erase(found);
        rescan();
        restoreSlot(taker);
        return true;
    }
    bool anyOptiScaler() const { return file_io::isFile(folder() / L"OptiScaler.ini") || state[Mod::optiscaler].present; }

    bool reshade(std::string method);
    bool reshadeShaders(RecordEntry& own);
    bool renodx(const std::string& method);
    bool optiscaler(const std::string& method);
    bool dlss(const std::string& method);
    bool dlss5();
    bool anr(const std::string& method);
    bool remove(Mod mod);
    bool removeAnr();
};

constexpr std::string_view default_reshade_ini =
    "[GENERAL]\r\nEffectSearchPaths=.\\reshade-shaders\\Shaders\\**\r\nTextureSearchPaths=.\\reshade-shaders\\Textures\\**\r\n"
    "PresetPath=.\\ReShadePreset.ini\r\n";

bool Job::reshade(std::string method) {
    const auto page = text(std::wstring(sources::reshade_page));
    if (!page) return false;
    const auto setup = sources::reshadeSetup(*page);
    if (!setup) return fail(tr(L"Nie znaleziono wersji ReShade na reshade.me."));
    const auto installer = cache("reshade/" + setup.version) / wide(setup.file);
    if (!file(setup.url, installer, L"ReShade " + wide(setup.version))) return false;
    zip_archive::Reader zip;
    const auto dll_name = state.x86 ? "ReShade32.dll" : "ReShade64.dll";
    std::optional<std::string> dll;
    if (zip.open(installer)) dll = zip.read(dll_name, 64 * 1024 * 1024);
    if (!dll) { std::error_code ec; fs::remove(installer, ec); return fail(tr(L"Instalator ReShade jest uszkodzony.")); }
    // Where it goes: beside OptiScaler as ReShade64.dll (OptiScaler loads it), over another tool's
    // ReShade in its place, else the chosen file.
    auto& own = entry(Mod::reshade);
    std::wstring slot = wide(methodOr(Mod::reshade, method));
    if (state[Mod::reshade].present && !state[Mod::reshade].ours) slot = wide(state[Mod::reshade].method);
    const bool beside_opti = anyOptiScaler();
    if (beside_opti) slot = reshadeSlot();
    // Another file than before (the method changed): the old one goes first.
    if (!own.files.empty() && own.files.front().path != folder() / slot) {
        auto old = own.files.front();
        RecordEntry single{own.version, own.method, {}, {old}, {}, {}};
        if (!removeFiles(single)) return fail(tr(L"Nie udało się usunąć poprzedniego pliku ReShade (czy gra działa?)."));
        own.files.erase(own.files.begin());
    }
    progress(tr(L"Instalowanie ReShade…"));
    if (!placeBytes(*dll, folder() / slot, own)) return fail(tr(L"Nie udało się zapisać ReShade w folderze gry (czy gra działa?)."));
    // Launchme's file first in the entry: the method's slot.
    std::stable_partition(own.files.begin(), own.files.end(), [&](const PlacedFile& placed) { return placed.path == folder() / slot; });
    if (!file_io::isFile(folder() / L"ReShade.ini")) placeBytes(default_reshade_ini, folder() / L"ReShade.ini", own);
    reshadeShaders(own);
    own.version = setup.version;
    own.method = beside_opti ? std::string(methodOr(Mod::reshade, method)) : utf8(slot);
    if (beside_opti) loadReshade(true);
    return save();
}

// The standard shaders (crosire's slim set) in reshade-shaders\, when the game has none: the folder is
// then ReShade's and goes with it.
bool Job::reshadeShaders(RecordEntry& own) {
    const auto target = folder() / L"reshade-shaders";
    std::error_code ec;
    if (fs::exists(target, ec)) return true;
    const auto archive = cache("reshade/shaders-slim.zip");
    if (!file(std::wstring(sources::reshade_shaders), archive, tr(L"shadery ReShade"))) { error.clear(); return false; } // ReShade works without them.
    zip_archive::Reader zip;
    if (!zip.open(archive)) return false;
    bool any = false;
    const auto& entries = zip.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& item = entries[i];
        if (item.folder || !zip_archive::safeName(item.name)) continue;
        // reshade-shaders-slim/Shaders/... and .../Textures/...
        const auto slash = item.name.find('/');
        if (slash == std::string::npos) continue;
        const auto inner = item.name.substr(slash + 1);
        if (!inner.starts_with("Shaders/") && !inner.starts_with("Textures/")) continue;
        if (zip.extract(static_cast<int>(i), target / fs::path(wide(inner)))) any = true;
    }
    if (any) own.folders.push_back(target);
    return any;
}

bool Job::renodx(const std::string& method) {
    if (!state[Mod::reshade].present && !reshade(request.reshade_method)) return false;
    rescan();
    const auto index_text = text(std::wstring(sources::renodx_index));
    if (!index_text) return false;
    const auto index = sources::renodxIndex(*index_text);
    const auto id = methodOr(Mod::renodx, method);
    const auto mod = id == "game" ? sources::renodxMatch(index, request.target.steam_appid, request.target.title, state.x86)
                                  : sources::renodxGeneric(index, id, state.x86);
    if (!mod) return fail(id == "game" ? tr(L"RenoDX nie ma dodatku dla tej gry. Na Unreal Engine lub Unity wybierz dodatek ogólny.") :
                                         tr(L"Nie znaleziono ogólnego dodatku RenoDX."));
    if (mod->x86 != state.x86) return fail(state.x86 ? tr(L"Dodatek RenoDX jest tylko dla gier 64-bitowych.") : tr(L"Dodatek RenoDX jest tylko dla gier 32-bitowych."));
    const auto addon = cache("renodx") / wide(mod->file);
    std::error_code ec;
    fs::remove(addon, ec); // The snapshot changes under the same name: always the newest.
    if (!file(std::wstring(sources::renodx_files) + wide(mod->file), addon, L"RenoDX · " + mod->title)) return false;
    auto& own = entry(Mod::renodx);
    if (!own.files.empty() && own.files.front().path.filename() != wide(mod->file) && !removeFiles(own))
        return fail(tr(L"Nie udało się usunąć poprzedniego dodatku RenoDX (czy gra działa?)."));
    if (!placeFile(addon, folder() / wide(mod->file), own)) return fail(tr(L"Nie udało się zapisać dodatku RenoDX (czy gra działa?)."));
    own.method = std::string(id);
    own.detail = wide(mod->file);
    own.version = mod->status;
    return save();
}

bool Job::optiscaler(const std::string& method) {
    if (state[Mod::anr].present && state[Mod::anr].method == "full") return fail(tr(L"OptiScaler jest już w pakiecie ANR FG."));
    const auto latest = text(std::wstring(sources::optiscaler_latest));
    if (!latest) return false;
    const auto release = sources::release(*latest);
    const auto* asset = release ? release->find(".7z") : nullptr;
    if (!asset) return fail(tr(L"Nie znaleziono wydania OptiScalera."));
    const auto version = release->tag.starts_with("v") ? release->tag.substr(1) : release->tag;
    const auto archive = cache("optiscaler/" + version) / wide(asset->name);
    if (!file(asset->url, archive, L"OptiScaler " + wide(version))) return false;
    const auto unpacked = cache("optiscaler/" + version) / L"files";
    if (!file_io::isFile(unpacked / L"OptiScaler.dll")) {
        progress(tr(L"Rozpakowywanie OptiScalera…"));
        std::error_code ec;
        fs::remove_all(unpacked, ec);
        fs::create_directories(unpacked, ec);
        if (!services.extract7z || !services.extract7z(archive, unpacked) || !file_io::isFile(unpacked / L"OptiScaler.dll"))
            return fail(tr(L"Nie udało się rozpakować OptiScalera (.7z)."));
    }
    const auto slot = wide(methodOr(Mod::optiscaler, method));
    // Launchme's earlier OptiScaler: its OptiScaler.ini (the user's settings) stays; it is taken out and
    // placed again (another file name, a newer version).
    std::optional<std::string> settings;
    if (const auto* old = record.find(Mod::optiscaler); old && !old->files.empty()) {
        settings = file_io::readText(folder() / L"OptiScaler.ini", 4 * 1024 * 1024);
        if (settings && settings->empty()) settings.reset();
        if (!takeOut(Mod::optiscaler)) return false;
        rescan();
    }
    auto& own = entry(Mod::optiscaler);
    if (!freeSlot(slot, own)) return false;
    progress(tr(L"Instalowanie OptiScalera…"));
    std::error_code ec;
    for (fs::recursive_directory_iterator it(unpacked, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) continue;
        const auto relative = it->path().lexically_relative(unpacked);
        const auto first = unicode_text::lower(relative.begin()->wstring());
        if (first == L"licenses" || first.starts_with(L"setup_") || first.starts_with(L"!!") || first.ends_with(L".txt")) continue;
        const auto target = first == L"optiscaler.dll" ? folder() / slot : folder() / relative;
        if (!placeFile(it->path(), target, own)) { removeFiles(own); record.mods.erase(slug(Mod::optiscaler)); save();
            return fail(tr(L"Nie udało się skopiować OptiScalera do folderu gry (czy gra działa?).")); }
    }
    if (settings) placeBytes(*settings, folder() / L"OptiScaler.ini", own);
    own.version = version;
    own.method = utf8(slot);
    rescan();
    if (reshadeAt(folder() / reshadeSlot())) loadReshade(true);
    return save();
}

bool Job::dlss(const std::string& method) {
    if (state.dlss_files.empty()) return fail(tr(L"Ta gra nie ma DLSS. Upscaler doda OptiScaler."));
    const auto manifest = text(std::wstring(sources::dlss_manifest));
    if (!manifest) return false;
    const bool all = methodOr(Mod::dlss, method) == "all";
    auto& own = entry(Mod::dlss);
    int replaced = 0;
    std::string newest;
    for (const auto& [file_name, kind] : {std::pair{L"nvngx_dlss.dll", "dlss"}, {L"nvngx_dlssd.dll", "dlssd"}, {L"nvngx_dlssg.dll", "dlssg"}}) {
        if (!all && kind != std::string_view("dlss")) continue;
        std::vector<fs::path> targets;
        for (const auto& found : state.dlss_files) if (unicode_text::lower(found.filename().wstring()) == file_name) targets.push_back(found);
        if (targets.empty()) continue;
        const auto source = sources::dlss(*manifest, kind);
        if (!source) continue;
        if (kind == std::string_view("dlss")) newest = source.version;
        // Only the files older than the newest (a game's newer DLL stays).
        std::erase_if(targets, [&](const fs::path& target) {
            return compareVersions(utf8(shortVersion(fileVersion(target))), source.version) >= 0 &&
                   std::none_of(own.files.begin(), own.files.end(), [&](const PlacedFile& placed) { return placed.path == target; });
        });
        if (targets.empty()) continue;
        const auto zip_file = cache("dlss") / wide(source.file);
        if (!file(source.url, zip_file, std::wstring(file_name) + L" " + wide(source.version))) return false;
        const auto dll = cache("dlss/" + std::string(kind) + "-" + source.version) / file_name;
        if (!file_io::isFile(dll)) {
            zip_archive::Reader zip;
            const int at = zip.open(zip_file) ? zip.find(utf8(file_name)) : -1;
            if (at < 0 || !zip.extract(at, dll)) { std::error_code ec; fs::remove(zip_file, ec); return fail(tr(L"Archiwum DLSS jest uszkodzone.")); }
        }
        progress(std::wstring(tr(L"Podmiana: ")) + file_name + L"…");
        for (const auto& target : targets) {
            if (!placeFile(dll, target, own)) { save(); return fail(std::wstring(tr(L"Nie udało się podmienić pliku (czy gra działa?): ")) + target.filename().wstring()); }
            ++replaced;
        }
    }
    if (!replaced && own.files.empty()) { record.mods.erase(slug(Mod::dlss)); return fail(tr(L"DLSS tej gry jest już w najnowszej wersji.")); }
    if (!newest.empty()) own.version = newest;
    own.method = std::string(methodOr(Mod::dlss, method));
    return save();
}

bool Job::dlss5() {
    if (state.x86) return fail(tr(L"DLSS 5 działa tylko w grach 64-bitowych."));
    if (!state[Mod::reshade].present && !reshade(request.reshade_method)) return false;
    rescan();
    const auto list = text(std::wstring(sources::rhi_releases));
    if (!list) return false;
    const auto addon_release = sources::newestTagged(*list, "renodx-dlss5-");
    const auto* addon_asset = addon_release ? addon_release->find(".zip") : nullptr;
    const auto manifest = text(std::wstring(sources::dlss_manifest));
    if (!manifest) return false;
    const auto nr = sources::dlss(*manifest, "dlssnr", "nvidia");
    if (!addon_asset || !nr) return fail(tr(L"Nie znaleziono plików DLSS 5."));
    auto& own = entry(Mod::dlss5);
    const auto unpack = [&](const fs::path& archive, std::wstring_view name, const fs::path& target) {
        if (file_io::isFile(target)) return true;
        zip_archive::Reader zip;
        const int at = zip.open(archive) ? zip.find(utf8(name)) : -1;
        if (at >= 0 && zip.extract(at, target)) return true;
        std::error_code ec;
        fs::remove(archive, ec);
        return fail(tr(L"Archiwum DLSS 5 jest uszkodzone."));
    };
    const auto addon_zip = cache("dlss5") / wide(addon_asset->name);
    const auto addon = cache("dlss5/" + addon_release->tag) / L"renodx-dlss5.addon64";
    if (!file(addon_asset->url, addon_zip, L"RenoDX DLSS 5") || !unpack(addon_zip, L"renodx-dlss5.addon64", addon)) return false;
    const auto nr_zip = cache("dlss") / wide(nr.file);
    const auto nr_dll = cache("dlss/dlssnr-" + nr.version) / L"nvngx_dlssnr.dll";
    if (!file(nr.url, nr_zip, L"nvngx_dlssnr.dll " + wide(nr.version)) || !unpack(nr_zip, L"nvngx_dlssnr.dll", nr_dll)) return false;
    progress(tr(L"Instalowanie DLSS 5…"));
    if (!placeFile(addon, folder() / L"renodx-dlss5.addon64", own) || !placeFile(nr_dll, folder() / L"nvngx_dlssnr.dll", own)) {
        save();
        return fail(tr(L"Nie udało się zapisać plików DLSS 5 (czy gra działa?)."));
    }
    // Its upscaler beside it when the program's folder has none of its own.
    const bool own_dlss = std::any_of(own.files.begin(), own.files.end(), [&](const PlacedFile& placed) { return placed.path == folder() / L"nvngx_dlss.dll"; });
    if (!own_dlss && !file_io::isFile(folder() / L"nvngx_dlss.dll"))
        if (const auto sr = sources::dlss(*manifest, "dlss")) {
            const auto sr_zip = cache("dlss") / wide(sr.file);
            const auto sr_dll = cache("dlss/dlss-" + sr.version) / L"nvngx_dlss.dll";
            if (file(sr.url, sr_zip, L"nvngx_dlss.dll " + wide(sr.version)) && unpack(sr_zip, L"nvngx_dlss.dll", sr_dll))
                placeFile(sr_dll, folder() / L"nvngx_dlss.dll", own);
            error.clear(); // DLSS 5 still works with the game's own (or OptiScaler's) inputs.
        }
    own.version = nr.version;
    own.method = "renodx";
    own.detail = wide(addon_release->tag.substr(std::string_view("renodx-dlss5-").size()));
    return save();
}

bool Job::anr(const std::string& method) {
    if (state.x86) return fail(tr(L"ANR FG działa tylko w grach 64-bitowych."));
    const bool opti = anyOptiScaler() && !(state[Mod::anr].present && state[Mod::anr].method == "full");
    std::string variant(methodOr(Mod::anr, method));
    if (variant == "auto") variant = opti ? "addon" : "full";
    if (variant == "full" && opti) return fail(tr(L"Gra ma już OptiScaler: wybierz „Dodatek do OptiScalera”."));
    if (variant == "addon" && !opti) return fail(tr(L"Dodatek ANR FG potrzebuje OptiScalera: zainstaluj go najpierw albo wybierz pakiet pełny."));
    const auto latest = text(std::wstring(sources::anr_latest));
    if (!latest) return false;
    const auto release = sources::release(*latest);
    const auto* asset = release ? release->find("-" + variant + ".zip") : nullptr;
    const auto* sums_asset = release ? release->find("SHA256SUMS.txt") : nullptr;
    if (!asset || !sums_asset) return fail(tr(L"Nie znaleziono wydania ANR FG."));
    const auto sums = text(sums_asset->url);
    if (!sums) return false;
    const auto expected = sources::checksum(*sums, asset->name);
    if (expected.empty()) return fail(tr(L"Brak sumy kontrolnej paczki ANR FG."));
    const auto version = release->tag.starts_with("FG-") ? release->tag.substr(3) : release->tag;
    const auto archive = cache("anr/" + version) / wide(asset->name);
    if (!file(asset->url, archive, L"ANR FG " + wide(version))) return false;
    if (hashing::fileHex(hashing::Algorithm::sha256, archive) != expected) {
        std::error_code ec;
        fs::remove(archive, ec);
        return fail(tr(L"Paczka ANR FG ma złą sumę kontrolną; usunięto ją."));
    }
    const auto package = cache("anr/" + version) / wide(variant);
    if (!file_io::isFile(package / L"install.ps1")) {
        std::error_code ec;
        fs::remove_all(package, ec);
        if (!extractZip(archive, package)) return fail(tr(L"Nie udało się rozpakować ANR FG."));
    }
    // An earlier version of Launchme's: out first (its own uninstaller), then the new one in.
    if (state[Mod::anr].ours && !removeAnr()) return false;
    rescan();
    auto& own = entry(Mod::anr);
    std::wstring proxy;
    if (variant == "full") {
        const auto chosen = methodOr(Mod::optiscaler, request.optiscaler_method);
        proxy = chosen == "winmm.dll" || chosen == "version.dll" ? wide(chosen) : std::wstring(L"dxgi.dll");
        if (!freeSlot(proxy, own)) return false;
    }
    progress(tr(L"Instalowanie ANR FG…"));
    if (!services.shell) return fail(tr(L"Nie można uruchomić instalatora ANR FG."));
    std::wstring command = L"& " + quotePowerShell((package / L"install.ps1").wstring()) + L" -Game " + quotePowerShell(folder().wstring());
    if (!proxy.empty()) command += L" -Proxy " + quotePowerShell(proxy);
    const auto result = services.shell(command);
    if (!result.ok()) {
        restoreSlot(own);
        record.mods.erase(slug(Mod::anr));
        save();
        const auto line = launch_scripts::lastLine(result.output);
        return fail(std::wstring(tr(L"Instalator ANR FG zgłosił błąd")) + (line.empty() ? std::wstring{} : L": " + line));
    }
    own.version = version;
    own.method = variant;
    own.detail = package.wstring();
    if (variant == "full") { rescan(); if (reshadeAt(folder() / reshadeSlot())) loadReshade(true); }
    return save();
}

bool Job::removeAnr() {
    const auto* own = record.find(Mod::anr);
    if (!own) return true;
    fs::path package = own->detail;
    if (!file_io::isFile(package / L"uninstall.ps1")) return fail(tr(L"Brak deinstalatora ANR FG w pamięci podręcznej: zainstaluj ANR FG ponownie, potem usuń."));
    progress(tr(L"Usuwanie ANR FG…"));
    const auto result = services.shell ? services.shell(L"& " + quotePowerShell((package / L"uninstall.ps1").wstring()) + L" -Game " + quotePowerShell(folder().wstring()))
                                       : launch_scripts::Result{};
    if (!result.ok()) {
        const auto line = launch_scripts::lastLine(result.output);
        return fail(std::wstring(tr(L"Deinstalator ANR FG zgłosił błąd")) + (line.empty() ? std::wstring{} : L": " + line));
    }
    auto taker = *own;
    // A file whose original Launchme moved aside for it (its proxy's slot): the original back.
    for (const auto& placed : taker.files) {
        std::error_code ec;
        if (!placed.backup.empty() && !fs::exists(placed.path, ec) && fs::exists(placed.backup, ec)) fs::rename(placed.backup, placed.path, ec);
    }
    record.mods.erase(slug(Mod::anr));
    rescan();
    restoreSlot(taker);
    return true;
}

bool Job::remove(Mod mod) {
    auto found = record.mods.find(slug(mod));
    if (found == record.mods.end()) {
        if (state[mod].present) return fail(tr(L"Ten mod zainstalowało inne narzędzie; Launchme usuwa tylko swoje."));
        return true;
    }
    switch (mod) {
    case Mod::reshade:
        // Its add-ons go with it.
        if (record.find(Mod::renodx) && !remove(Mod::renodx)) return false;
        if (record.find(Mod::dlss5) && !remove(Mod::dlss5)) return false;
        found = record.mods.find(slug(mod));
        break;
    case Mod::optiscaler:
        if (state[Mod::anr].present && state[Mod::anr].method == "addon") {
            if (!state[Mod::anr].ours) return fail(tr(L"Najpierw usuń ANR FG (dodatek do OptiScalera)."));
            if (!removeAnr()) return false;
            found = record.mods.find(slug(mod));
        }
        break;
    case Mod::anr: return removeAnr() && save();
    default: break;
    }
    progress(std::wstring(tr(L"Usuwanie: ")) + name(mod) + L"…");
    if (!takeOut(mod)) return false;
    if (mod == Mod::reshade) loadReshade(false);
    return save();
}
} // namespace

std::wstring quotePowerShell(std::wstring_view text) {
    std::wstring quoted = L"'";
    for (const wchar_t c : text) {
        quoted += c;
        if (c == L'\'' || c == L'‘' || c == L'’') quoted += c; // PowerShell treats the curly quotes as quotes too.
    }
    return quoted + L"'";
}

bool extractZip(const fs::path& archive, const fs::path& folder) {
    zip_archive::Reader zip;
    if (!zip.open(archive)) return false;
    const auto& entries = zip.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].folder) continue;
        if (!zip_archive::safeName(entries[i].name)) return false;
        if (!zip.extract(static_cast<int>(i), folder / fs::path(wide(entries[i].name)))) return false;
    }
    return true;
}

Outcome apply(const Request& request, const fs::path& data, const Services& services) {
    Outcome outcome;
    const auto& target = request.target;
    std::error_code ec;
    if (target.folder.empty() || !fs::is_directory(target.folder, ec)) {
        outcome.message = tr(L"Nie znaleziono folderu gry.");
        return outcome;
    }
    Job job{request, data, services, {}, {}, {}};
    job.record = loadRecord(data, target.folder).value_or(Record{});
    job.record.folder = target.folder;
    job.rescan();
    if (!request.remove && !job.state.anticheat.empty() && !request.allow_anticheat) {
        outcome.problem = Problem::anticheat;
        std::wstring names;
        for (const auto& name : job.state.anticheat) names += (names.empty() ? L"" : L", ") + name;
        outcome.message = std::wstring(tr(L"Gra ma zabezpieczenie: ")) + names + tr(L". Mod może skończyć się banem w grze sieciowej. Naciśnij jeszcze raz, by zainstalować.");
        return outcome;
    }
    const auto method = request.method;
    bool ok = false;
    if (request.remove) ok = job.remove(request.mod);
    else switch (request.mod) {
    case Mod::reshade: ok = job.reshade(method.empty() ? request.reshade_method : method); break;
    case Mod::renodx: ok = job.renodx(method); break;
    case Mod::optiscaler: ok = job.optiscaler(method); break;
    case Mod::dlss: ok = job.dlss(method); break;
    case Mod::dlss5: ok = job.dlss5(); break;
    case Mod::anr: ok = job.anr(method); break;
    }
    if (!ok) { outcome.message = job.error.empty() ? std::wstring(tr(L"Nie udało się.")) : job.error; return outcome; }
    outcome.problem = Problem::none;
    if (request.remove) outcome.message = std::wstring(name(request.mod)) + tr(L" usunięty z gry.");
    else {
        const auto* entry = job.record.find(request.mod);
        outcome.message = std::wstring(name(request.mod)) + (entry && !entry->version.empty() && request.mod != Mod::renodx ? L" " + wide(entry->version) : std::wstring{}) +
                          tr(L" zainstalowany.");
        if (request.mod == Mod::renodx || request.mod == Mod::dlss5) outcome.message += tr(L" Wyłącz Auto HDR i RTX HDR dla tej gry.");
        if (request.mod == Mod::optiscaler || (request.mod == Mod::anr && job.record.find(Mod::anr) && job.record.find(Mod::anr)->method == "full"))
            outcome.message += tr(L" Menu OptiScalera: klawisz Insert.");
        if (request.mod == Mod::reshade) outcome.message += tr(L" Menu ReShade: klawisz Home.");
    }
    return outcome;
}
} // namespace game_mods

namespace game_mods {
Services defaultServices(std::function<bool()> cancelled, std::function<void(std::wstring_view)> progress) {
    Services services;
    services.cancelled = cancelled;
    services.progress = std::move(progress);
    services.fetch = [](const std::wstring& url) {
        const bool api = url.starts_with(L"https://api.github.com/");
        return httpGet(url, api ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n" : L"", 16 * 1024 * 1024);
    };
    services.download = [cancelled](const std::wstring& url, const fs::path& file) {
        // Large files (DLSS 5's model: 270 MB) arrive in several requests, each resuming the last.
        for (int attempt = 0; attempt < 12; ++attempt) {
            if (cancelled && cancelled()) return false;
            const auto response = httpDownloadFile(url, L"", file, 1024ull * 1024 * 1024, 5 * 60 * 1000, false);
            if (response.status == 200) return true;
            if (response.status >= 400) return false;
        }
        return false;
    };
    services.shell = [cancelled](std::wstring_view command) {
        launch_scripts::Context context{launch_scripts::Phase::before, L"Launchme", "mods"};
        return launch_scripts::run(command, context, std::chrono::minutes(10), cancelled);
    };
    services.extract7z = [shell = services.shell](const fs::path& archive, const fs::path& folder) {
#ifdef _WIN32
        // Windows' own bsdtar (libarchive with LZMA) reads .7z.
        const auto result = shell(L"& \"$env:SystemRoot\\System32\\tar.exe\" -xf " + quotePowerShell(archive.wstring()) + L" -C " + quotePowerShell(folder.wstring()) +
                                  L"; exit $LASTEXITCODE");
        return result.ok();
#else
        (void)shell; (void)archive; (void)folder;
        return false;
#endif
    };
    return services;
}
} // namespace game_mods
