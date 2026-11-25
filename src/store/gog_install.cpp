#include "gog_install.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <set>

namespace gog {
namespace {
std::string field(const json::Value& value, const char* key) { const auto item = value.get(key); return item ? item->stringOr() : std::string{}; }
std::optional<uint64_t> number(std::string_view text) {
    uint64_t n = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
    return error == std::errc{} && end == text.data() + text.size() ? std::optional{n} : std::nullopt;
}
bool identifier(std::string_view text) {
    return !text.empty() && text.size() <= 128 && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
bool plainDirectory(const std::filesystem::path& path) {
    if (!path.is_absolute() || path == path.root_path() || path.wstring().find_first_of(L"\r\n\"") != std::wstring::npos) return false;
    // A selected parent may contain ordinary directories, but a task must not
    // follow junctions/symlinks when resuming or deleting its own downloads.
    auto current = path;
    while (!current.empty() && current != current.root_path()) {
#ifdef _WIN32
        const auto attrs = GetFileAttributesW(current.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#else
        std::error_code ec;
        if (std::filesystem::is_symlink(current, ec)) return false;
#endif
        current = current.parent_path();
    }
    return true;
}
bool ordinaryFile(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND;
    return !(attrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
#else
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec) return status.type() == std::filesystem::file_type::not_found;
    return status.type() != std::filesystem::file_type::symlink && status.type() != std::filesystem::file_type::directory;
#endif
}
}
bool installerFilename(std::wstring_view name) {
    if (name.empty() || name.size() > 180 || name.front() == L'.' || name.back() == L'.' || name.back() == L' ') return false;
    if (name.find_first_of(L"/\\:\"<>|?*\r\n") != name.npos ||
        std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32 || c > 126; })) return false;
    auto lower = std::wstring(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
    if (!lower.ends_with(L".exe") && !lower.ends_with(L".bin")) return false;
    const auto stem = lower.substr(0, lower.find('.'));
    return stem != L"con" && stem != L"prn" && stem != L"aux" && stem != L"nul" &&
        !(stem.size() == 4 && (stem.starts_with(L"com") || stem.starts_with(L"lpt")) && stem[3] >= L'0' && stem[3] <= L'9');
}
bool installerDownloadUrl(std::wstring_view url) {
    if (!url.starts_with(L"https://") || url.size() > 16384 || url.find_first_of(L"\r\n\\#@") != url.npos) return false;
    // The authority ends at the first of these, as WinHTTP and libcurl read it: with only '/',
    // "https://evil.test?.gog.com/x" would pass as a GOG host and connect to evil.test.
    const auto end = url.find_first_of(L"/?#:", 8);
    if (end == url.npos || url[end] != L'/') return false; // A path right after the host: no port, no query.
    std::wstring host(url.substr(8, end - 8));
    for (auto& ch : host) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'.')) return false;
    }
    // The resolver issues signed URLs on GOG's CDN domains; do not send a
    // bearer or follow a redirect to a URL selected by a downloaded document.
    return host == L"gog.com" || host.ends_with(L".gog.com") || host == L"gog-statics.com" || host.ends_with(L".gog-statics.com");
}
std::optional<InstallerPart> parseInstallerChecksum(std::string_view xml, std::wstring url) {
    if (!installerDownloadUrl(url) || xml.size() > 2 * 1024 * 1024 || xml.find("<!") != xml.npos) return {};
    const auto start = xml.find("<file ");
    const auto end = xml.find('>', start);
    if (start == xml.npos || end == xml.npos) return {};
    std::map<std::string, std::string> attrs;
    auto text = xml.substr(start + 6, end - start - 6);
    while (!text.empty()) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) text.remove_prefix(1);
        if (text.empty() || text == "/") break;
        const auto equals = text.find('=');
        if (equals == text.npos) return {};
        const std::string key(text.substr(0, equals)); text.remove_prefix(equals + 1);
        if (text.empty() || (text.front() != '"' && text.front() != '\'')) return {};
        const char quote = text.front(); text.remove_prefix(1);
        const auto close = text.find(quote);
        if (close == text.npos || !attrs.emplace(key, text.substr(0, close)).second) return {};
        text.remove_prefix(close + 1);
    }
    InstallerPart part{text_encoding::wide(attrs["name"]), std::move(url), attrs["md5"]};
    const auto size = number(attrs["total_size"]);
    if (attrs["available"] != "1" || !installerFilename(part.name) || !size || !*size || *size > (1ull << 40) ||
        part.md5.size() != 32 || part.md5.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return {};
    std::transform(part.md5.begin(), part.md5.end(), part.md5.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    part.bytes = *size; return part;
}
std::optional<std::vector<InstallerVariant>> parseInstallers(std::string_view text, const std::string& product) {
    if (product.empty() || product.size() > 96 || product.find_first_not_of("0123456789") != product.npos) return {};
    const auto root = json::parse(text);
    const auto id = root ? root->get("id") : nullptr;
    const auto downloads = root ? root->get("downloads") : nullptr;
    const auto list = downloads ? downloads->get("installers") : nullptr;
    if (!id || id->text != product || !list || list->type != json::Value::Type::array || list->elements.size() > 256) return {};
    std::vector<InstallerVariant> result;
    std::set<std::string> seen;
    for (const auto& item : list->elements) {
        if (field(item, "os") != "windows") continue;
        InstallerVariant variant;
        variant.choice.id = field(item, "id");
        variant.choice.language = text_encoding::wide(field(item, "language_full"));
        if (variant.choice.language.empty()) variant.choice.language = text_encoding::wide(field(item, "language"));
        variant.choice.version = text_encoding::wide(field(item, "version"));
        const auto bytes = item.get("total_size");
        const auto size = bytes ? number(bytes->text) : std::nullopt;
        const auto files = item.get("files");
        if (!identifier(variant.choice.id) || !seen.insert(variant.choice.id).second || variant.choice.language.empty() ||
            variant.choice.language.size() > 128 || variant.choice.version.size() > 128 || !size || !*size ||
            !files || files->type != json::Value::Type::array || files->elements.empty() || files->elements.size() > 256) return {};
        variant.choice.bytes = *size;
        for (const auto& file : files->elements) {
            const auto link = field(file, "downlink");
            const auto prefix = "https://api.gog.com/products/" + product + "/downlink/installer/";
            if (!link.starts_with(prefix) || !identifier(std::string_view(link).substr(prefix.size()))) return {};
            variant.downlinks.push_back(text_encoding::wide(link));
        }
        result.push_back(std::move(variant));
    }
    return result;
}
std::wstring silentInstallerArguments(const std::filesystem::path& directory) {
    auto path = directory.lexically_normal().wstring();
    if (!directory.is_absolute() || path.find_first_of(L"\r\n\"") != path.npos || path.find(L'\0') != path.npos) return {};
    while (!path.empty() && path.back() == L'\\') path.pop_back();
    return L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCLOSEAPPLICATIONS /NORESTARTAPPLICATIONS /DIR=\"" + path + L"\"";
}
InstallError runOfflineInstaller(const InstallRequest& request, const std::vector<InstallerPart>& parts,
    const InstallOperations& ops, const InstallCancel& cancel, const InstallProgress& progress) {
    namespace fs = std::filesystem;
    if (parts.empty() || !identifier(request.product) || !plainDirectory(request.download_root) ||
        !plainDirectory(request.install_directory) || silentInstallerArguments(request.install_directory).empty()) return InstallError::storage;
    uint64_t total = 0; std::set<std::wstring> names; std::wstring executable;
    std::string manifest = "launchme-gog-download-v1\n" + request.product + '\n' + request.variant + '\n';
    for (const auto& part : parts) {
        auto lower = part.name; std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
        if (!installerFilename(part.name) || !names.insert(lower).second || !part.bytes || part.bytes > (1ull << 40) ||
            part.md5.size() != 32 || part.md5.find_first_not_of("0123456789abcdef") != part.md5.npos) return InstallError::manifest;
        if (lower.ends_with(L".exe")) { if (!executable.empty()) return InstallError::manifest; executable = part.name; }
        total += part.bytes;
        manifest += text_encoding::utf8(part.name) + ' ' + part.md5 + ' ' + std::to_string(part.bytes) + '\n';
    }
    if (executable.empty()) return InstallError::manifest;
    // Content identity, not a signed URL, defines the resumable job directory.
    // Full file hashes remain in the ownership marker, including split BINs.
    const auto folder = request.download_root / (L"Launchme-GOG-" + text_encoding::wide(request.product)) / text_encoding::wide(parts.front().md5);
    if (!plainDirectory(folder)) return InstallError::storage;
    std::error_code ec;
    fs::create_directories(folder.parent_path(), ec); if (ec) return InstallError::storage;
    const bool created = fs::create_directory(folder, ec); if (ec) return InstallError::storage;
    const auto marker = folder / L".launchme-download";
    if (created) { if (!atomic_file::write(marker, manifest)) return InstallError::storage; }
    else {
        if (!ordinaryFile(marker)) return InstallError::storage;
        std::ifstream in(marker, std::ios::binary | std::ios::ate);
        if (!in || in.tellg() != static_cast<std::streamoff>(manifest.size())) return InstallError::storage;
        std::string previous(manifest.size(), '\0'); in.seekg(0); in.read(previous.data(), static_cast<std::streamsize>(previous.size()));
        if (!in || previous != manifest) return InstallError::storage;
    }
    uint64_t completed = 0;
    for (const auto& part : parts) {
        if (cancel()) return InstallError::none;
        const auto file = folder / part.name, partial = folder / (part.name + L".part");
        if (!ordinaryFile(file) || !ordinaryFile(partial)) return InstallError::storage;
        bool verified = false;
        if (fs::exists(file, ec)) {
            progress(InstallState::verifying, 0, 0);
            const auto bytes = fs::file_size(file, ec);
            if (ec) return InstallError::storage;
            verified = bytes == part.bytes && ops.checksum(file, part.md5, cancel);
            if (cancel()) return InstallError::none;
            if (!verified) {
                // The matching ownership manifest was checked above. Repair
                // only this corrupt ready part; the other verified parts stay.
                if (!ordinaryFile(file)) return InstallError::storage;
                fs::remove(partial, ec); // A stale partial must not become its resume baseline.
                if (ec) return InstallError::storage;
                if (!fs::remove(file, ec) || ec) return InstallError::storage;
            }
        } else if (ec) return InstallError::storage;
        if (!verified) {
            progress(InstallState::downloading, completed, total);
            const auto error = ops.download(part, partial, cancel, [&](uint64_t bytes) {
                progress(InstallState::downloading, completed + bytes, total);
            });
            if (cancel()) return InstallError::none;
            if (error != InstallError::none) return error;
            progress(InstallState::verifying, 0, 0);
            if (!ops.checksum(partial, part.md5, cancel)) {
                if (!cancel()) fs::remove(partial, ec); // Invalid bytes must never become an executable or a resume baseline.
                return cancel() ? InstallError::none : InstallError::checksum;
            }
            if (cancel()) return InstallError::none;
            if (!atomic_file::moveNew(partial, file)) return InstallError::storage;
        }
        completed += part.bytes;
    }
    if (cancel()) return InstallError::none;
    progress(InstallState::installing, 0, 0);
    const auto error = ops.execute(folder / executable, request, cancel);
    if (cancel()) return InstallError::none;
    if (error != InstallError::none) return error;
    if (!ops.detect(request)) return InstallError::detection;
    // Do not recurse. Unknown user files and the actual game directory are
    // never part of cleanup, and a failed installer retains every download.
    for (const auto& part : parts) {
        if (!ordinaryFile(folder / part.name) || !fs::remove(folder / part.name, ec) || ec) return InstallError::cleanup;
    }
    fs::remove(marker, ec); if (ec) return InstallError::cleanup;
    fs::remove(folder, ec); // An unrelated file can keep this directory alive.
    progress(InstallState::completed, total, total);
    return InstallError::none;
}
std::wstring installStatusText(const InstallSnapshot& value) {
    switch (value.error) {
    case InstallError::account: return tr(L"Połącz konto GOG z tą grą, a następnie ponów.");
    case InstallError::unavailable: return tr(L"GOG nie udostępnia instalatora Windows dla tej gry.");
    case InstallError::network: return tr(L"Nie udało się pobrać plików. Ponów, aby wznowić.");
    case InstallError::manifest: return tr(L"Nieprawidłowe dane instalatora GOG.");
    case InstallError::storage: return tr(L"Nie można zapisać plików. Sprawdź folder i wolne miejsce.");
    case InstallError::checksum: return tr(L"Plik nie przeszedł sprawdzenia. Ponów pobieranie.");
    case InstallError::installer: return tr(L"Instalator nie zakończył się pomyślnie. Pliki zachowano.");
    case InstallError::detection: return tr(L"Nie potwierdzono instalacji w wybranym folderze. Pliki zachowano.");
    case InstallError::cleanup: return tr(L"Zainstalowano; nie udało się usunąć wszystkich plików instalatora.");
    default: break;
    }
    switch (value.state) {
    case InstallState::loading: return tr(L"Pobieranie listy instalatorów…");
    case InstallState::ready: return tr(L"Wybierz język i foldery.");
    case InstallState::downloading: return installationText(value.progress());
    case InstallState::verifying: return tr(L"Sprawdzanie pobranych plików…");
    case InstallState::installing: return tr(L"Instalowanie…");
    case InstallState::completed: return tr(L"Zainstalowano.");
    case InstallState::cancelled: return tr(L"Pobieranie przerwane. Pliki zachowano do wznowienia.");
    default: return L"";
    }
}
}
