// Kopia i przenoszenie: what each part holds, the export and the staged import (profile_transfer.hpp).
// Applying a staged import: profile_transfer_apply.cpp.
#include "profile_transfer.hpp"
#include "profile_transfer_internal.hpp"
#include "atomic_file.hpp"
#include "password_crypto.hpp"
#include "protected_file.hpp"
#include "text_encoding.hpp"
#include "zip_archive.hpp"
#include "../net/json.hpp"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>

namespace profile_transfer {
using namespace transfer_detail;

const char* slug(Part part) {
    switch (part) {
    case Part::settings: return "settings";
    case Part::library: return "library";
    case Part::artwork: return "artwork";
    case Part::artwork_cache: return "artwork-cache";
    case Part::keys: return "keys";
    case Part::accounts: return "accounts";
    case Part::plugins: return "plugins";
    case Part::companion: return "companion";
    }
    return "";
}
std::optional<Part> partFromSlug(std::string_view text) {
    for (const auto part : parts) if (text == slug(part)) return part;
    return std::nullopt;
}
bool needsPassword(Part part) {
    return part == Part::keys || part == Part::accounts || part == Part::plugins || part == Part::companion;
}
bool Manifest::has(Part part) const {
    return std::any_of(parts.begin(), parts.end(), [&](const auto& entry) { return entry.first == part; });
}

namespace transfer_detail {
std::string entryPath(const std::filesystem::path& relative) { return text_encoding::utf8Lenient(relative.generic_wstring()); }
std::filesystem::path fromEntry(std::string_view name) {
    std::filesystem::path path;
    size_t start = 0;
    while (start <= name.size()) {
        const auto end = std::min(name.find('/', start), name.size());
        path /= text_encoding::wideLenient(name.substr(start, end - start));
        start = end + 1;
    }
    return path;
}
std::optional<std::string> readFile(const std::filesystem::path& file, uint64_t limit) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || static_cast<uint64_t>(input.tellg()) > limit) return std::nullopt;
    std::string bytes(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!bytes.empty() && !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return std::nullopt;
    return bytes;
}
bool regularFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    return !ec && std::filesystem::is_regular_file(status);
}
std::string systemName() {
#ifdef _WIN32
    return "windows";
#else
    return "linux";
#endif
}
}

namespace {
constexpr uint64_t secure_limit = 1024ull * 1024 * 1024;
constexpr uint64_t file_limit = 4ull * 1024 * 1024 * 1024;
bool temporaryName(const std::wstring& name) {
    return name.find(L".tmp.") != std::wstring::npos || name.ends_with(L".tmp") || name.ends_with(L".lock") || name.ends_with(L".part");
}
// Every regular file under config/folder (links are never followed), relative to config.
void tree(const std::filesystem::path& config, const std::filesystem::path& folder, std::vector<std::filesystem::path>& out,
          const std::function<bool(const std::filesystem::path&)>& keep = {}) {
    std::error_code ec;
    const auto root = config / folder;
    const auto status = std::filesystem::symlink_status(root, ec);
    if (ec || !std::filesystem::is_directory(status)) return;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec)) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec) || temporaryName(it->path().filename().wstring())) continue;
        const auto relative = std::filesystem::relative(it->path(), config, ec);
        if (ec || relative.empty()) { ec.clear(); continue; }
        if (!keep || keep(relative)) out.push_back(relative);
    }
}
void file(const std::filesystem::path& config, const std::filesystem::path& relative, std::vector<std::filesystem::path>& out) {
    if (regularFile(config / relative)) out.push_back(relative);
}
// Files of the data folder itself (no subfolders) whose names `keep` accepts.
void rootFiles(const std::filesystem::path& config, const std::function<bool(const std::wstring&)>& keep, std::vector<std::filesystem::path>& out) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(config, ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().wstring();
        if (!it->is_symlink(ec) && it->is_regular_file(ec) && !temporaryName(name) && keep(name)) out.push_back(it->path().filename());
    }
}
bool keyFile(const std::wstring& name) {
    if (name == L"protected.key") return false; // Linux: the key of this user's own protected files.
    return name.ends_with(L"_key.txt") || name.ends_with(L"_key.dat") || name.ends_with(L".key") || name.ends_with(L"_token.dat") ||
           name.ends_with(L"_token.txt") || name.starts_with(L"screenscraper_") || name.starts_with(L"gog_client_") ||
           name == L"gog_redirect_uri.txt";
}
const wchar_t* const artwork_folders[] = {L"covers", L"logos", L"heroes", L"posters", L"landscapes", L"stores"};
}

namespace transfer_detail {
std::vector<std::filesystem::path> collect(const std::filesystem::path& config, Part part) {
    std::vector<std::filesystem::path> out;
    switch (part) {
    case Part::settings:
        for (const auto* folder : {L"themes", L"libraries", L"wallpapers", L"fonts"}) tree(config, folder, out);
        tree(config, std::filesystem::path(L"platforms") / L"backgrounds", out);
        file(config, L"controller_overrides.json", out);
        break;
    case Part::library:
        for (const auto* name : {L"custom-games.json", L"drm-free.json", L"metadata_overrides.json", L"play-journal.tsv", L"achievement-history.json"})
            file(config, name, out);
        file(config, std::filesystem::path(L"linux") / L"launch_options.json", out);
        tree(config, L"metadata", out, [](const std::filesystem::path& path) { return path.filename().wstring().ends_with(L".rawg.json"); });
        break;
    case Part::artwork:
        tree(config, std::filesystem::path(L"artwork") / L"selections", out);
        // Hand-set crops (*.manual-focus) and the cached pictures they belong to.
        for (const auto* folder : artwork_folders)
            tree(config, folder, out, [&](const std::filesystem::path& path) {
                if (path.extension() != L".manual-focus") return false;
                auto image = path; image.replace_extension();
                if (regularFile(config / image)) out.push_back(image);
                return true;
            });
        break;
    case Part::artwork_cache:
        for (const auto* folder : artwork_folders)
            tree(config, folder, out, [](const std::filesystem::path& path) { return path.extension() != L".manual-focus"; });
        break;
    case Part::keys: rootFiles(config, keyFile, out); break;
    case Part::accounts: tree(config, L"accounts", out); break;
    case Part::plugins:
        file(config, std::filesystem::path(L"plugins") / L"registry.dat", out);
        tree(config, std::filesystem::path(L"plugins") / L"packages", out);
        tree(config, std::filesystem::path(L"plugins") / L"data", out);
        break;
    case Part::companion:
        rootFiles(config, [](const std::wstring& name) {
            return name.starts_with(L"companion_") && !keyFile(name) && name != L"companion_vectors.dat";
        }, out);
        tree(config, L"companion", out);
        break;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
// keys and accounts are secret as a whole; plugins and the companion only where this user's key protects a file.
bool wholeSecret(Part part) { return part == Part::keys || part == Part::accounts; }
bool maybeProtected(const std::filesystem::path& relative) {
    const auto extension = relative.extension().wstring();
    return extension == L".dat" || extension == L".bin" || extension == L".session" || extension == L".library" || extension == L".key";
}

std::string packSecure(const std::vector<SecureFile>& files) {
    std::string out = "LMSECURE1\n";
    const auto number = [&](uint64_t value, int bytes) { for (int i = 0; i < bytes; ++i) out += static_cast<char>((value >> (8 * i)) & 0xFF); };
    for (const auto& item : files) {
        const auto path = entryPath(item.relative);
        const std::string_view part = slug(item.part);
        number(part.size(), 1); out += part;
        number(item.protect ? 1 : 0, 1);
        number(path.size(), 4); out += path;
        number(item.data.size(), 8); out += item.data;
    }
    return out;
}
std::optional<std::vector<SecureFile>> unpackSecure(std::string_view bytes) {
    constexpr std::string_view head = "LMSECURE1\n";
    if (!bytes.starts_with(head)) return std::nullopt;
    size_t at = head.size();
    const auto number = [&](int count, uint64_t& value) {
        if (bytes.size() - at < static_cast<size_t>(count)) return false;
        value = 0;
        for (int i = 0; i < count; ++i) value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes[at + i])) << (8 * i);
        at += count; return true;
    };
    const auto text = [&](uint64_t size, std::string& value) {
        if (bytes.size() - at < size) return false;
        value.assign(bytes.substr(at, static_cast<size_t>(size))); at += static_cast<size_t>(size); return true;
    };
    std::vector<SecureFile> files;
    while (at < bytes.size()) {
        uint64_t size = 0, flag = 0;
        std::string part_slug, path;
        SecureFile item;
        if (!number(1, size) || !text(size, part_slug) || !number(1, flag) || !number(4, size) || !text(size, path) ||
            !number(8, size) || !text(size, item.data)) return std::nullopt;
        const auto part = partFromSlug(part_slug);
        if (!part || flag > 1 || !zip_archive::safeName(path)) return std::nullopt;
        item.part = *part; item.protect = flag == 1; item.relative = fromEntry(path);
        files.push_back(std::move(item));
    }
    return files;
}
}

PartSize measure(const std::filesystem::path& config, Part part) {
    PartSize size;
    std::error_code ec;
    for (const auto& relative : collect(config, part)) {
        const auto bytes = std::filesystem::file_size(config / relative, ec);
        if (!ec) { ++size.files; size.bytes += bytes; }
        ec.clear();
    }
    if (part == Part::settings && regularFile(config / L"settings.ini")) ++size.files;
    if (part == Part::library && regularFile(config / L"library-state.ini")) ++size.files;
    return size;
}

std::wstring backupName(uint64_t unix_seconds) {
    const auto time = static_cast<std::time_t>(unix_seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    wchar_t text[64]{};
    std::wcsftime(text, std::size(text), L"launchme_backup_%Y-%m-%d--%H-%M.zip", &local);
    return text;
}

namespace {
std::string isoNow() {
    const auto time = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}
struct Cleanup {
    std::filesystem::path path;
    bool folder = false;
    ~Cleanup() {
        if (path.empty()) return;
        std::error_code ec;
        if (folder) std::filesystem::remove_all(path, ec); else std::filesystem::remove(path, ec);
    }
};
}

Result exportProfile(const ExportInput& input) {
    Result result;
    const auto fail = [&](std::wstring error) { result.ok = false; result.error = std::move(error); return result; };
    if (input.parts.empty()) return fail(L"Nothing chosen to export.");
    const bool secret = std::any_of(input.parts.begin(), input.parts.end(), needsPassword);
    if (secret && input.password.empty()) return fail(L"A password is needed for keys, accounts, plugins and the companion.");
    std::error_code ec;
    if (!std::filesystem::is_directory(input.folder, ec)) return fail(L"The folder does not exist: " + input.folder.wstring());

    // What goes in: plain files per part, secret ones read out of this user's protection.
    struct Plain { Part part; std::filesystem::path relative; uint64_t size; };
    std::vector<Plain> plain;
    std::vector<SecureFile> secure;
    std::vector<std::pair<Part, PartInfo>> info;
    uint64_t total = 0;
    for (const auto part : parts) {
        if (!input.parts.contains(part)) continue;
        PartInfo counted;
        for (const auto& relative : collect(input.config, part)) {
            const auto source = input.config / relative;
            const auto size = std::filesystem::file_size(source, ec);
            if (ec) { ec.clear(); continue; }
            if (needsPassword(part)) {
                SecureFile item{part, relative};
                if (auto opened = (wholeSecret(part) || maybeProtected(relative)) ? protected_file::read(source, secure_limit) : std::nullopt) {
                    item.data = std::move(*opened); item.protect = true;
                }
                else if (wholeSecret(part)) {
                    auto raw = readFile(source, secure_limit);
                    if (!raw) continue;
                    item.data = std::move(*raw);
                } else { plain.push_back({part, relative, size}); total += size; ++counted.files; counted.bytes += size; continue; }
                counted.bytes += item.data.size(); ++counted.files; total += item.data.size();
                secure.push_back(std::move(item));
                continue;
            }
            plain.push_back({part, relative, size}); total += size; ++counted.files; counted.bytes += size;
        }
        if (part == Part::settings || part == Part::library) ++counted.files;
        info.emplace_back(part, counted);
    }

    // The settings and the library state as this launcher has them (unsaved changes too).
    const auto scratch = input.folder / (L".launchme-export-" + std::to_wstring(atomic_file::detail::processId()));
    Cleanup scratch_cleanup{scratch, true};
    std::filesystem::create_directories(scratch, ec);
    std::optional<std::string> settings_text, library_text;
    if (input.parts.contains(Part::settings)) {
        if (input.settings) {
            if (!saveAppSettings(*input.settings, scratch / L"settings.ini")) return fail(L"Cannot write the settings.");
            settings_text = readFile(scratch / L"settings.ini", 16 * 1024 * 1024);
        } else settings_text = readFile(input.config / L"settings.ini", 16 * 1024 * 1024);
        if (!settings_text) settings_text = std::string{};
    }
    if (input.parts.contains(Part::library)) {
        if (input.library) {
            if (!saveLibraryState(*input.library, scratch / L"library-state.ini")) return fail(L"Cannot write the library state.");
            library_text = readFile(scratch / L"library-state.ini", max_library_state_bytes);
        } else library_text = readFile(input.config / L"library-state.ini", max_library_state_bytes);
    }

    std::optional<std::string> sealed;
    if (!secure.empty()) {
        auto packed = packSecure(secure);
        for (auto& item : secure) std::fill(item.data.begin(), item.data.end(), '\0');
        sealed = password_crypto::seal(input.password, packed);
        std::fill(packed.begin(), packed.end(), '\0');
        if (!sealed) return fail(L"Cannot encrypt the keys.");
    }

    auto manifest = json::object();
    manifest.properties["format"] = json::number(format_version);
    manifest.properties["app"] = json::string("launchme");
    manifest.properties["app_version"] = json::string(input.app_version);
    manifest.properties["system"] = json::string(systemName());
    manifest.properties["created"] = json::string(isoNow());
    manifest.properties["config"] = json::string(text_encoding::utf8Lenient(input.config.wstring()));
    manifest.properties["sealed"] = json::boolean(sealed.has_value());
    auto list = json::array();
    for (const auto& [part, counted] : info) {
        auto item = json::object();
        item.properties["part"] = json::string(slug(part));
        item.properties["files"] = json::number(static_cast<double>(counted.files));
        item.properties["bytes"] = json::number(static_cast<double>(counted.bytes));
        list.elements.push_back(std::move(item));
    }
    manifest.properties["parts"] = std::move(list);

    // A new file next to the others: never over an existing backup.
    const auto name = backupName(static_cast<uint64_t>(std::time(nullptr)));
    auto target = input.folder / name;
    for (int suffix = 2; std::filesystem::exists(target, ec) && suffix < 1000; ++suffix)
        target = input.folder / (std::filesystem::path(name).stem().wstring() + L"-" + std::to_wstring(suffix) + L".zip");
    const auto partial = scratch / L"backup.zip";
    zip_archive::Writer zip;
    if (!zip.open(partial)) return fail(L"Cannot create the file in " + input.folder.wstring());
    uint64_t done = 0;
    const auto step = [&](uint64_t bytes) { done += bytes; if (input.progress) input.progress(done, total); };
    const auto cancelled = [&] { return input.cancel && input.cancel(); };
    if (!zip.add("manifest.json", json::serialize(manifest))) return fail(L"Cannot write the backup.");
    if (settings_text && !zip.add("settings/settings.ini", *settings_text)) return fail(L"Cannot write the backup.");
    if (library_text && !zip.add("library/library-state.ini", *library_text)) return fail(L"Cannot write the backup.");
    if (sealed && !zip.add("secure.bin", *sealed, true)) return fail(L"Cannot write the backup.");
    for (const auto& item : plain) {
        if (cancelled()) { result.cancelled = true; return fail(L"Cancelled."); }
        if (item.size > file_limit) { step(item.size); continue; }
        const auto entry = std::string(slug(item.part)) + "/files/" + entryPath(item.relative);
        if (!zip_archive::safeName(entry)) { step(item.size); continue; }
        if (!zip.addFile(entry, input.config / item.relative, zip_archive::storedType(item.relative)))
            return fail(L"Cannot pack " + item.relative.wstring());
        ++result.files;
        step(item.size);
    }
    if (!zip.finish()) return fail(L"Cannot finish the backup (disk full?).");
    if (!atomic_file::moveNew(partial, target)) return fail(L"Cannot save " + target.wstring());
    if (input.progress) input.progress(total, total);
    result.ok = true;
    result.path = target;
    result.files += secure.size() + (settings_text ? 1 : 0) + (library_text ? 1 : 0);
    result.bytes = std::filesystem::file_size(target, ec);
    return result;
}

std::optional<Manifest> inspect(const std::filesystem::path& file, std::wstring& error) {
    zip_archive::Reader zip;
    if (!zip.open(file)) { error = L"This is not a ZIP file, or it is damaged."; return std::nullopt; }
    const auto text = zip.read("manifest.json", 1024 * 1024);
    const auto parsed = text ? json::parse(*text) : std::nullopt;
    if (!parsed || parsed->type != json::Value::Type::object || parsed->get("app") == nullptr || parsed->get("app")->stringOr() != "launchme") {
        error = L"This is not a Launchme backup (no manifest.json)."; return std::nullopt;
    }
    Manifest manifest;
    manifest.format = static_cast<uint32_t>(parsed->get("format") ? parsed->get("format")->numeric : 0);
    if (manifest.format != format_version) { error = L"This backup comes from a newer Launchme; update it first."; return std::nullopt; }
    if (const auto* value = parsed->get("app_version")) manifest.app_version = value->stringOr();
    if (const auto* value = parsed->get("system")) manifest.system = value->stringOr();
    if (const auto* value = parsed->get("created")) manifest.created = value->stringOr();
    if (const auto* value = parsed->get("config")) manifest.config = text_encoding::wideLenient(value->stringOr());
    if (const auto* value = parsed->get("sealed")) manifest.sealed = value->flag;
    if (const auto* list = parsed->get("parts"); list && list->type == json::Value::Type::array)
        for (const auto& item : list->elements) {
            const auto* name = item.get("part");
            const auto part = name ? partFromSlug(name->stringOr()) : std::nullopt;
            if (!part || manifest.has(*part)) continue;
            PartInfo counted;
            if (const auto* files = item.get("files")) counted.files = static_cast<size_t>(std::max(0.0, files->numeric));
            if (const auto* bytes = item.get("bytes")) counted.bytes = static_cast<uint64_t>(std::max(0.0, bytes->numeric));
            manifest.parts.emplace_back(*part, counted);
        }
    // Every entry must be a known one with a name that stays inside its folder.
    for (const auto& entry : zip.entries()) {
        if (entry.folder) continue;
        const auto& name = entry.name;
        bool known = name == "manifest.json" || name == "secure.bin" || name == "settings/settings.ini" || name == "library/library-state.ini";
        for (const auto part : parts)
            if (name.starts_with(std::string(slug(part)) + "/files/")) known = true;
        if (!known || !zip_archive::safeName(name)) { error = L"The backup holds an unexpected file: " + text_encoding::wideLenient(name); return std::nullopt; }
    }
    if (manifest.sealed && zip.find("secure.bin") < 0) { error = L"The backup's encrypted part is missing."; return std::nullopt; }
    return manifest;
}

PathMap parsePathMap(std::wstring_view text) {
    PathMap map;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find_first_of(L";\n", start);
        if (end == std::wstring_view::npos) end = text.size();
        const auto rule = text.substr(start, end - start);
        start = end + 1;
        const auto arrow = rule.find(L"=>");
        if (arrow == std::wstring_view::npos) continue;
        const auto trim = [](std::wstring_view value) {
            const auto first = value.find_first_not_of(L" \t\"");
            if (first == std::wstring_view::npos) return std::wstring{};
            const auto last = value.find_last_not_of(L" \t\"");
            return std::wstring(value.substr(first, last - first + 1));
        };
        auto from = trim(rule.substr(0, arrow)), to = trim(rule.substr(arrow + 2));
        if (!from.empty() && !to.empty()) map.emplace_back(std::move(from), std::move(to));
    }
    return map;
}

bool pending(const std::filesystem::path& config) {
    std::error_code ec;
    return std::filesystem::is_regular_file(config / pending_folder / L"plan.json", ec);
}
void discardPending(const std::filesystem::path& config) {
    std::error_code ec;
    std::filesystem::remove_all(config / pending_folder, ec);
}

Result stageImport(const ImportInput& input) {
    Result result;
    const auto fail = [&](std::wstring error) { result.ok = false; result.error = std::move(error); return result; };
    std::wstring error;
    const auto manifest = inspect(input.zip, error);
    if (!manifest) return fail(error);
    std::set<Part> chosen;
    for (const auto part : input.parts) if (manifest->has(part)) chosen.insert(part);
    if (chosen.empty()) return fail(L"Nothing chosen to import.");
    zip_archive::Reader zip;
    if (!zip.open(input.zip)) return fail(L"Cannot read the backup.");

    std::vector<SecureFile> secure;
    const bool secret = std::any_of(chosen.begin(), chosen.end(), needsPassword);
    if (secret && manifest->sealed) {
        const auto bytes = zip.read("secure.bin", secure_limit);
        if (!bytes) return fail(L"Cannot read the backup's encrypted part.");
        auto opened = password_crypto::open(input.password, *bytes);
        if (!opened) return fail(L"Wrong password.");
        auto unpacked = unpackSecure(*opened);
        std::fill(opened->begin(), opened->end(), '\0');
        if (!unpacked) return fail(L"The backup's encrypted part is damaged.");
        secure = std::move(*unpacked);
    }

    // Unpacked next to the profile first, then put in place as a whole.
    const auto stage = input.config / L"import-pending.new";
    std::error_code ec;
    std::filesystem::remove_all(stage, ec);
    Cleanup stage_cleanup{stage, true};
    if (!std::filesystem::create_directories(stage, ec) && ec) return fail(L"Cannot write to " + input.config.wstring());

    uint64_t total = 0, done = 0;
    std::vector<int> entries;
    for (size_t i = 0; i < zip.entries().size(); ++i) {
        const auto& entry = zip.entries()[i];
        if (entry.folder) continue;
        bool wanted = (entry.name == "settings/settings.ini" && chosen.contains(Part::settings)) ||
                      (entry.name == "library/library-state.ini" && chosen.contains(Part::library));
        for (const auto part : chosen) if (entry.name.starts_with(std::string(slug(part)) + "/files/")) wanted = true;
        if (!wanted) continue;
        entries.push_back(static_cast<int>(i));
        total += entry.size;
    }
    for (const auto& item : secure) if (chosen.contains(item.part)) total += item.data.size();
    for (const int index : entries) {
        if (input.cancel && input.cancel()) { result.cancelled = true; return fail(L"Cancelled."); }
        const auto& entry = zip.entries()[static_cast<size_t>(index)];
        if (!zip.extract(index, stage / fromEntry(entry.name))) return fail(L"Cannot unpack " + text_encoding::wideLenient(entry.name));
        ++result.files;
        done += entry.size;
        if (input.progress) input.progress(done, total);
    }
    for (auto& item : secure) {
        if (!chosen.contains(item.part)) continue;
        const auto target = stage / text_encoding::wideLenient(slug(item.part)) / L"files" / item.relative;
        // Protected again, now for this user on this computer.
        const bool written = item.protect ? protected_file::write(target, item.data, L"Launchme") : atomic_file::write(target, item.data);
        std::fill(item.data.begin(), item.data.end(), '\0');
        if (!written) return fail(L"Cannot protect " + item.relative.wstring() + L" for this user.");
        ++result.files;
        done += item.data.size();
        if (input.progress) input.progress(done, total);
    }

    auto plan = json::object();
    plan.properties["mode"] = json::string(input.mode == Mode::replace ? "replace" : "merge");
    plan.properties["system"] = json::string(manifest->system);
    auto chosen_list = json::array();
    for (const auto part : chosen) chosen_list.elements.push_back(json::string(slug(part)));
    plan.properties["parts"] = std::move(chosen_list);
    auto paths = json::array();
    const auto add_rule = [&](const std::wstring& from, const std::wstring& to) {
        auto rule = json::array();
        rule.elements.push_back(json::string(text_encoding::utf8Lenient(from)));
        rule.elements.push_back(json::string(text_encoding::utf8Lenient(to)));
        paths.elements.push_back(std::move(rule));
    };
    for (const auto& [from, to] : input.paths) add_rule(from, to);
    // The backup's own data folder is this one now (wallpapers, sound files kept in it).
    if (!manifest->config.empty()) add_rule(manifest->config, input.config.wstring());
    plan.properties["paths"] = std::move(paths);
    plan.properties["source"] = json::string(entryPath(input.zip));
    if (!atomic_file::write(stage / L"plan.json", json::serialize(plan))) return fail(L"Cannot write the import plan.");

    discardPending(input.config);
    if (!atomic_file::moveNew(stage, input.config / pending_folder)) return fail(L"Cannot prepare the import in " + input.config.wstring());
    stage_cleanup.path.clear();
    if (input.progress) input.progress(total, total);
    result.ok = true;
    result.path = input.config / pending_folder;
    result.bytes = total;
    return result;
}
}
