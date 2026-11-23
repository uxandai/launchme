#include "manager.hpp"
#include "package.hpp"
#include "downloads.hpp"
#include "achievements.hpp"
#include "../../sdk/launchme_plugin.h"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/system.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <atomic>
#include <algorithm>
#include <charconv>

namespace plugins {
using namespace wire;
namespace {
void loadIcon(Plugin& plugin, const std::filesystem::path& directory) {
    if (plugin.manifest.icon.empty()) return;
    try {
        const auto bytes = readFile(directory / plugin.manifest.icon, 256 * 1024);
        plugin.icon = std::make_shared<const std::vector<uint8_t>>(bytes.begin(), bytes.end());
    } catch (...) { plugin.icon.reset(); } // A missing optional picture retains the monogram.
}
struct Response { std::string bytes; unsigned calls = 0; bool invalid = false; };
void LAUNCHME_CALL emit(void* context, const char* bytes, uint32_t length) {
    auto& out = *static_cast<Response*>(context);
    if (++out.calls != 1 || !bytes || length > LAUNCHME_PLUGIN_MAX_JSON) { out.invalid = true; return; }
    try { out.bytes.assign(bytes, length); } catch (...) { out.invalid = true; }
}
std::string unique() {
    static std::atomic_uint64_t serial{0};
    return std::to_string(system_info::processId()) + '-' + std::to_string(GetTickCount64()) + '-' + std::to_string(serial++);
}
std::optional<std::array<uint32_t, 3>> version(std::string_view text) {
    std::array<uint32_t, 3> out{};
    for (size_t i = 0; i < out.size(); ++i) {
        const auto end = text.find('.'); const auto part = text.substr(0, end);
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), out[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || (i < 2) != (end != text.npos)) return {};
        if (end != text.npos) text.remove_prefix(end + 1);
    }
    return out;
}
json::Value request(std::string_view method, const json::Value& params, const json::Value& settings, std::string_view locale) {
    auto result = object(); result.properties = {{"method", string(std::string(method))}, {"params", params}, {"settings", settings}, {"locale", string(std::string(locale))}};
    return result;
}
}
struct Manager::Module {
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
    const LaunchmePluginV1* api = nullptr;
    bool activated = false;
    json::Value call(const json::Value& value) {
        const auto bytes = encode(value);
        if (bytes.size() > LAUNCHME_PLUGIN_MAX_JSON) throw std::runtime_error("Plugin request exceeds limit");
        Response out;
        int32_t status = -1;
        try { status = api->invoke(bytes.data(), static_cast<uint32_t>(bytes.size()), emit, &out); }
        catch (...) { throw std::runtime_error("Plugin threw across the C ABI"); }
        const auto parsed = json::parse(out.bytes);
        if (status || out.invalid || out.calls != 1 || !parsed || parsed->type != json::Value::Type::object)
            throw std::runtime_error("Invalid plugin response");
        if (!text(*parsed, "error").empty()) throw std::runtime_error(text(*parsed, "error"));
        return *parsed;
    }
    ~Module() {
        if (activated) try { call(request("deactivate", object(), object(), "en")); } catch (...) {}
#ifdef _WIN32
        if (handle) FreeLibrary(handle);
#else
        if (handle) dlclose(handle);
#endif
    }
};
const Plugin* Snapshot::find(const std::string& id) const {
    for (const auto& p : plugins) if (p.manifest.id == id) return &p;
    return nullptr;
}
std::vector<Action> Snapshot::contributions(std::string_view kind, const Game* game) const {
    std::vector<Action> result;
    for (const auto& p : plugins) if (p.enabled && p.error.empty())
        for (const auto& c : p.manifest.contributions)
            if (c.kind == kind && (!game || matches(c, *game))) result.push_back({p.manifest.id, c});
    return result;
}
Manager::Manager(std::filesystem::path root) : root_(std::move(root)) {}
Manager::~Manager() = default;
bool Manager::initialized() const { return std::filesystem::exists(root_ / L"registry.dat"); }
Manager::Entry& Manager::get(const std::string& id) {
    const auto it = entries_.find(id); if (it == entries_.end()) throw std::runtime_error("Plugin is not installed"); return it->second;
}
void Manager::save(const std::map<std::string, Entry>& entries) {
    auto state = object();
    json::Value bundled; bundled.type = json::Value::Type::array;
    for (const auto& id : bundled_) bundled.elements.push_back(string(id));
    state.properties["@bundled"] = std::move(bundled);
    for (const auto& [id, e] : entries) {
        auto item = object(); item.properties = {{"revision", string(e.revision)}, {"enabled", boolean(e.plugin.enabled)}, {"settings", e.plugin.settings}, {"error", string(e.plugin.error)}};
        state.properties.emplace(id, std::move(item));
    }
    auto bytes = encode(state);
    const bool ok = protected_file::write(root_ / L"registry.dat", bytes, L"Launchme plugins");
    SecureZeroMemory(bytes.data(), bytes.size());
    if (!ok) throw std::runtime_error("Cannot save plugin settings");
}
std::shared_ptr<Manager::Module> Manager::activate(const Entry& entry) {
    const auto dll = std::filesystem::absolute(root_ / L"packages" / entry.revision / entry.plugin.manifest.entry);
    validateDll(dll);
    auto module = std::make_shared<Module>();
#ifdef _WIN32
    module->handle = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module->handle) throw std::runtime_error("Cannot load plugin DLL (error " + std::to_string(GetLastError()) + ')');
    const auto getter = reinterpret_cast<LaunchmeGetPluginV1>(GetProcAddress(module->handle, "launchme_plugin_v1"));
#else
    // RTLD_LOCAL: one plugin's symbols never satisfy another's.
    module->handle = dlopen(dll.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module->handle) { const char* error = dlerror(); throw std::runtime_error(std::string("Cannot load plugin library: ") + (error ? error : "unknown error")); }
    const auto getter = reinterpret_cast<LaunchmeGetPluginV1>(dlsym(module->handle, "launchme_plugin_v1"));
#endif
    module->api = getter ? getter(LAUNCHME_PLUGIN_ABI) : nullptr;
    if (!module->api || module->api->size < sizeof(LaunchmePluginV1) || module->api->abi != LAUNCHME_PLUGIN_ABI || !module->api->invoke)
        throw std::runtime_error("Plugin ABI mismatch");
    auto params = object();
    params.properties["dataDirectory"] = string(text_encoding::utf8(std::filesystem::absolute(root_ / L"data" / entry.plugin.manifest.id).wstring()));
    std::filesystem::create_directories(root_ / L"data" / entry.plugin.manifest.id);
    module->activated = true; // A partial activation is still followed by deactivate.
    module->call(request("activate", params, entry.plugin.settings, "en"));
    return module;
}
void Manager::load(const std::string& crashed) {
    entries_.clear();
    bundled_.clear();
    if (!initialized()) return;
    auto bytes = protected_file::read(root_ / L"registry.dat", 1024 * 1024);
    if (!bytes) throw std::runtime_error("Cannot decrypt plugin registry");
    const auto state = json::parse(*bytes);
    SecureZeroMemory(bytes->data(), bytes->size());
    if (!state || state->type != json::Value::Type::object || state->properties.size() > 65) throw std::runtime_error("Invalid plugin registry");
    for (const auto& id : field(*state, "@bundled").elements) if (identifier(id.stringOr())) bundled_.insert(id.text);
    for (const auto& [id, value] : state->properties) {
        if (id == "@bundled") continue;
        Entry entry; entry.revision = text(value, "revision"); entry.plugin.manifest.id = id; entry.plugin.manifest.name = {text_encoding::wide(id), text_encoding::wide(id)};
        try {
            if (!identifier(id) || !packagePath(entry.revision) || !entry.revision.starts_with(id + '/')) throw std::runtime_error("Invalid installed package path");
            entry.plugin.manifest = parseManifest(readFile(root_ / L"packages" / entry.revision / L"plugin.json", 64 * 1024));
            if (entry.plugin.manifest.id != id) throw std::runtime_error("Installed plugin ID mismatch");
            loadIcon(entry.plugin, root_ / L"packages" / entry.revision);
            const auto& saved = field(value, "settings");
            for (const auto& s : entry.plugin.manifest.settings) {
                const auto& v = field(saved, s.id); entry.plugin.settings.properties[s.id] = s.accepts(v) ? v : s.initial;
            }
            entry.plugin.error = text(value, "error");
            entry.plugin.enabled = field(value, "enabled").flag;
            // Its code crashed the previous run: not loaded, and off until the user switches it on.
            if (entry.plugin.enabled && id == crashed) { entry.plugin.enabled = false; entry.plugin.error = "Crashed Launchme; switched off"; }
            if (entry.plugin.enabled) entry.module = activate(entry);
        } catch (const std::exception& e) { entry.plugin.enabled = false; entry.plugin.error = e.what(); }
        entries_.emplace(id, std::move(entry));
    }
    save(entries_); // Persist load/activation failures as disabled; no retry loop on restart.
}
std::string Manager::install(const std::filesystem::path& zip) {
    if (entries_.size() >= 64) throw std::runtime_error("Plugin limit reached");
    std::filesystem::create_directories(root_ / L"packages");
    auto staging = root_ / L"packages" / ("stage-" + unique());
    struct Cleanup { std::filesystem::path path; ~Cleanup() { if (!path.empty()) { std::error_code ec; std::filesystem::remove_all(path, ec); } } } cleanup{staging};
    Entry next; next.plugin.manifest = extractPackage(zip, staging);
    loadIcon(next.plugin, staging);
    const auto id = next.plugin.manifest.id;
    const auto old = entries_.find(id);
    const auto old_revision = old != entries_.end() ? old->second.revision : std::string{};
    next.revision = id + '/' + unique();
    for (const auto& s : next.plugin.manifest.settings) {
        const auto& value = old != entries_.end() ? field(old->second.plugin.settings, s.id) : s.initial;
        next.plugin.settings.properties[s.id] = s.accepts(value) ? value : s.initial;
    }
    next.plugin.enabled = old != entries_.end() && old->second.plugin.enabled;
    const auto destination = root_ / L"packages" / next.revision;
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::rename(staging, destination); cleanup.path = destination;
    if (next.plugin.enabled) next.module = activate(next);
    auto updated = entries_; updated[id] = std::move(next);
    save(updated); entries_.swap(updated); cleanup.path.clear();
    updated.clear(); // Release old DLL before removing its immutable revision.
    if (!old_revision.empty()) { std::error_code ec; std::filesystem::remove_all(root_ / L"packages" / old_revision, ec); }
    return id;
}
void Manager::ensureBundled(const std::string& id, const std::filesystem::path& zip) {
    if ((bundled_.contains(id) && !entries_.contains(id)) || !std::filesystem::exists(zip)) return;
    const auto manifest = inspectPackage(zip);
    if (manifest.id != id) throw std::runtime_error("Bundled package ID mismatch");
    const bool existing = entries_.contains(id);
    if (existing) {
        const auto before = version(get(id).plugin.manifest.version), after = version(manifest.version);
        if (before && after && *after > *before) install(zip); // Upgrade, preserving disabled/enabled state and settings.
        if (bundled_.contains(id)) return;
    } else install(zip);
    bundled_.insert(id);
    try {
        if (!existing) enable(id, true); // First install: enabled out of the box.
        else save(entries_); // Never override a previously installed plugin's preference.
    } catch (...) { bundled_.erase(id); throw; }
}
void Manager::enable(const std::string& id, bool enabled) {
    auto next = get(id); next.plugin.error.clear(); next.plugin.enabled = enabled;
    if (enabled && !next.module) next.module = activate(next);
    if (!enabled) next.module.reset();
    auto updated = entries_; updated[id] = std::move(next); save(updated); entries_.swap(updated);
}
void Manager::setting(const std::string& id, const std::string& key, const json::Value& value) {
    auto next = get(id);
    const auto& defs = next.plugin.manifest.settings;
    const auto s = std::find_if(defs.begin(), defs.end(), [&](const auto& item) { return item.id == key; });
    if (s == defs.end() || !s->accepts(value)) throw std::runtime_error("Invalid plugin setting");
    next.plugin.settings.properties[key] = value;
    auto updated = entries_; updated[id] = std::move(next); save(updated); entries_.swap(updated);
}
void Manager::remove(const std::string& id) {
    get(id);
    if (!identifier(id)) throw std::runtime_error("Invalid plugin ID");
    auto updated = entries_; updated.erase(id); save(updated); entries_.swap(updated);
    updated.clear(); // deactivation and FreeLibrary precede deletion, including when enabled.
    std::error_code ec; std::filesystem::remove_all(root_ / L"packages" / id, ec);
    if (ec) throw std::runtime_error("Plugin unregistered; package files could not be removed");
}
json::Value Manager::invoke(const std::string& id, const std::string& method, const json::Value& params, const std::string& locale) {
    auto& e = get(id);
    if (!e.plugin.enabled || !e.module) throw std::runtime_error("Plugin is disabled");
    if (method != "command" && method != "search" && method != "widget" && method != "details" && method != "library" && method != "downloads" && method != "achievements" && method != "launch")
        throw std::runtime_error("Unknown plugin method");
    if (method == "launch" && std::none_of(e.plugin.manifest.contributions.begin(), e.plugin.manifest.contributions.end(),
        [](const auto& c) { return c.kind == "launch"; })) throw std::runtime_error("Plugin has no launch provider");
    if (method == "achievements" && std::none_of(e.plugin.manifest.contributions.begin(), e.plugin.manifest.contributions.end(),
        [](const auto& c) { return c.kind == "achievements"; })) throw std::runtime_error("Plugin has no achievements provider");
    if (method == "downloads" && std::none_of(e.plugin.manifest.contributions.begin(), e.plugin.manifest.contributions.end(),
        [](const auto& c) { return c.kind == "downloads"; })) throw std::runtime_error("Plugin has no download provider");
    if (method == "library" && std::none_of(e.plugin.manifest.contributions.begin(), e.plugin.manifest.contributions.end(),
        [](const auto& c) { return c.kind == "library"; })) throw std::runtime_error("Plugin has no library supplement");
    try {
        auto response = e.module->call(request(method, params, e.plugin.settings, locale));
        if (method == "library") parseLibrarySupplement(response);
        if (method == "downloads") parseDownloads(response);
        if (method == "achievements") parseAchievements(response);
        if (method == "command" && response.get("downloads")) parseDownloads(response);
        if (method == "command") validateQueued(response);
        if (method == "command") if (const auto* reveal = response.get("revealFile")) {
            const auto path = text_encoding::wide(reveal->stringOr());
            if (reveal->type != json::Value::Type::string || path.empty() || path.size() > 32768 || path.find_first_of(std::wstring(L"\0\r\n", 3)) != path.npos ||
                !std::filesystem::path(path).is_absolute()) throw std::runtime_error("Invalid reveal path");
        }
        return response;
    }
    catch (const std::exception& error) {
        // Network/application errors use {message:...}; protocol errors quarantine the module.
        e.plugin.error = error.what(); e.plugin.enabled = false; e.module.reset();
        try { save(entries_); } catch (...) {} // Never reactivate a faulted module in this process.
        throw;
    }
}
Snapshot Manager::snapshot() const {
    Snapshot out;
    for (const auto& [id, e] : entries_) {
        auto p = e.plugin;
        for (const auto& s : p.manifest.settings) if (s.type == "secret")
            p.settings.properties[s.id] = string(text(p.settings, s.id).empty() ? "" : "set");
        out.plugins.push_back(std::move(p));
    }
    return out;
}
}
