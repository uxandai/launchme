#include "../src/store/gog_install.hpp"
#include "../src/net/http_download.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
constexpr auto md5 = "900150983cd24fb0d6963f7d28e17f72"; // MD5 of the synthetic bytes "abc".
const gog::InstallCancel running = [] { return false; };
void parsing() {
    const auto xml = R"(<?xml version="1.0"?><file name="setup_fixture.exe" available="1" md5="900150983cd24fb0d6963f7d28e17f72" total_size="3"><chunk/></file>)";
    const auto part = gog::parseInstallerChecksum(xml, L"https://cdn.gog.com/secure/fixture");
    check(part && part->bytes == 3 && part->name == L"setup_fixture.exe", "checksum XML exact size/name/hash");
    check(!gog::parseInstallerChecksum(xml, L"https://gog.com.evil.test/file"), "no foreign signed download URL");
    for (const auto* url : {L"https://evil.test?.gog.com/file", L"https://evil.test:443.gog.com/file", L"https://cdn.gog.com:8443/file",
                            L"https://evil.test%2f.gog.com/file", L"https://cdn.gog.com?x=/file"})
        check(!gog::installerDownloadUrl(url), "the host ends where WinHTTP and libcurl end it");
    check(gog::installerDownloadUrl(L"https://CDN.GOG.com/secure/file?token=1") && gog::installerDownloadUrl(L"https://gog-statics.com/a"),
          "GOG hosts, any case, a query after the path");
    check(!gog::parseInstallerChecksum("<!DOCTYPE file>" + std::string(xml), L"https://cdn.gog.com/file"), "no XML entity expansion");
    for (const auto* name : {L"../setup.exe", L"C:\\setup.exe", L"setup.exe:stream", L"CON.exe", L"setup.exe.", L"setup.bat", L".exe"})
        check(!gog::installerFilename(name), "unsafe filename rejected");
    const auto catalog = R"({"id":123,"downloads":{"installers":[{"id":"windows_en","os":"windows","language":"en","language_full":"English","version":null,"total_size":6,"files":[{"downlink":"https://api.gog.com/products/123/downlink/installer/en1installer0"},{"downlink":"https://api.gog.com/products/123/downlink/installer/en1installer1"}]},{"id":"linux_en","os":"linux"}]}})";
    const auto choices = gog::parseInstallers(catalog, "123");
    check(choices && choices->size() == 1 && choices->front().downlinks.size() == 2, "Windows multipart choices");
    check(!gog::parseInstallers(catalog, "124"), "wrong product metadata rejected");
    auto changed = std::string(catalog); changed.replace(changed.find("api.gog.com"), 11, "evil.test");
    check(!gog::parseInstallers(changed, "123"), "bearer cannot be sent to foreign downlink");
    check(validDownloadRange(L"bytes 4294967296-9999999999/10000000000", 4294967296, 10000000000), "64-bit range resume");
    check(!validDownloadRange(L"bytes 0-99/100", 50, 100), "wrong resume start rejected");
    check(!validDownloadRange(L"bytes 50-98/100", 50, 100), "incomplete range rejected");
    check(!validDownloadRange(L"bytes 0-18446744073709551616/100", 0, 100), "overflow rejected");
    check(gog::silentInstallerArguments(L"C:\\Games\\Example\\").ends_with(L"/DIR=\"C:\\Games\\Example\""), "silent directory quoting");
    check(gog::silentInstallerArguments(L"C:\\bad\" /RUN").empty(), "argument injection rejected");
    gog::InstallSnapshot status; status.state = gog::InstallState::downloading; status.done = 3; status.total = 6;
    check(status.progress().percent() == 50, "real download percentage");
    status.state = gog::InstallState::installing;
    check(!status.progress().percent() && status.progress().moving(), "installing spins without fabricated percentage");
}
void lifecycle(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    gog::InstallRequest request{"123", "windows_en", root / "downloads", root / "game", 0};
    fs::create_directories(request.download_root);
    std::vector<gog::InstallerPart> parts{{L"setup_fixture.exe", L"https://cdn.gog.com/fixture", md5, 3}, {L"setup_fixture-1.bin", L"https://cdn.gog.com/fixture-bin", md5, 3}};
    auto operations = gog::nativeInstallOperations();
    int downloads = 0, executions = 0; bool fail_installer = true;
    std::vector<gog::InstallState> states;
    fs::path job;
    operations.download = [&](const auto&, const fs::path& file, const auto&, const auto& progress) {
        ++downloads; job = file.parent_path();
        std::ofstream(file, std::ios::binary) << "abc"; progress(3);
        return gog::InstallError::none;
    };
    operations.execute = [&](const fs::path& file, const auto&, const auto&) {
        ++executions;
        check(fs::exists(file) && fs::exists(file.parent_path() / parts[1].name), "all parts must be verified before starting installer");
        check(states.back() == gog::InstallState::installing, "phase set before launch");
        return fail_installer ? gog::InstallError::installer : gog::InstallError::none;
    };
    operations.detect = [](const auto&) { return true; };
    const auto report = [&](gog::InstallState state, uint64_t done, uint64_t total) {
        check(state == gog::InstallState::downloading || !total || state == gog::InstallState::completed, "no installer percentage");
        check(done <= total, "bounded progress"); states.push_back(state);
    };
    check(gog::runOfflineInstaller(request, parts, operations, running, report) == gog::InstallError::installer, "failed installer status");
    check(downloads == 2 && executions == 1 && fs::exists(job / parts[0].name) && fs::exists(job / parts[1].name), "failure retains multipart download");
    std::ofstream(job / "unrelated-user-file.txt") << "keep";
    fail_installer = false;
    check(gog::runOfflineInstaller(request, parts, operations, running, report) == gog::InstallError::none, "retry completed");
    check(downloads == 2 && executions == 2, "retry reuses verified parts");
    check(!fs::exists(job / parts[0].name) && !fs::exists(job / parts[1].name) && fs::exists(job / "unrelated-user-file.txt"), "cleanup only owns its installer files");
    check(states.back() == gog::InstallState::completed, "completion after cleanup");

    request.product = "456";
    bool cancelled = false;
    operations.download = [&](const auto&, const fs::path& file, const auto&, const auto& progress) {
        std::ofstream(file, std::ios::binary) << "a"; job = file.parent_path(); progress(1); cancelled = true;
        return gog::InstallError::none;
    };
    check(gog::runOfflineInstaller(request, parts, operations, [&] { return cancelled; }, report) == gog::InstallError::none && executions == 2, "cancel never starts installer");
    check(fs::file_size(job / (parts[0].name + L".part")) == 1, "cancel leaves resumable partial");
    cancelled = false;
    operations.download = [&](const auto&, const fs::path& file, const auto&, const auto& progress) {
        std::ofstream(file, std::ios::binary | std::ios::app) << "bc"; progress(3); return gog::InstallError::none;
    };
    const std::vector<gog::InstallerPart> one{parts[0]};
    check(gog::runOfflineInstaller(request, one, operations, running, report) == gog::InstallError::storage, "changed manifest cannot resume a different task");
    operations.download = [&](const auto&, const fs::path& file, const auto&, const auto& progress) {
        std::ofstream(file, std::ios::binary) << "bad"; progress(3); return gog::InstallError::none;
    };
    check(gog::runOfflineInstaller(request, parts, operations, running, report) == gog::InstallError::checksum && executions == 2, "bad download is never executed");
    check(!fs::exists(job / (parts[0].name + L".part")), "bad partial removed for a clean retry");
    auto duplicate = parts; duplicate[1].name = L"SETUP_FIXTURE.EXE";
    check(gog::runOfflineInstaller(request, duplicate, operations, running, report) == gog::InstallError::manifest, "case-insensitive filename collisions");
}
void repairCachedParts(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    gog::InstallRequest request{"789", "windows_en", root / L"repair", root / L"game", 0};
    const std::vector<gog::InstallerPart> parts{{L"setup.exe", L"https://cdn.gog.com/test", md5, 3}, {L"data.bin", L"https://cdn.gog.com/test", md5, 3}};
    auto operations = gog::nativeInstallOperations();
    int downloads = 0, executions = 0;
    bool installer_fails = true, cancelled = false;
    fs::path folder;
    operations.download = [&](const auto&, const fs::path& partial, const auto&, const auto&) {
        ++downloads; folder = partial.parent_path();
        check(!fs::exists(partial), "corrupt ready file must not reuse an old partial");
        std::ofstream(partial, std::ios::binary) << "abc";
        return gog::InstallError::none;
    };
    operations.execute = [&](const auto&, const auto&, const auto&) { ++executions; return installer_fails ? gog::InstallError::installer : gog::InstallError::none; };
    operations.detect = [](const auto&) { return true; };
    const auto run = [&] { return gog::runOfflineInstaller(request, parts, operations, [&] { return cancelled; }, [](auto, auto, auto) {}); };
    check(run() == gog::InstallError::installer && downloads == 2 && executions == 1, "retain two verified parts after installer failure");
    std::ofstream(folder / L"unrelated.txt") << "keep";
    std::ofstream(folder / L"setup.exe", std::ios::binary) << "bad";
    std::ofstream(folder / L"setup.exe.part", std::ios::binary) << "old";
    check(run() == gog::InstallError::installer && downloads == 3 && executions == 2, "redownload only corrupt EXE, reuse valid BIN");
    std::ofstream(folder / L"data.bin", std::ios::binary) << "x";
    check(run() == gog::InstallError::installer && downloads == 4 && executions == 3, "redownload truncated BIN, reuse valid EXE");

    std::ofstream(folder / L"setup.exe", std::ios::binary) << "bad";
    const auto locked = CreateFileW((folder / L"setup.exe").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "lock fixture");
    const auto failed_delete = run(); CloseHandle(locked);
    check(failed_delete == gog::InstallError::storage && downloads == 4 && executions == 3, "failed deletion neither downloads nor executes");
    const auto checksum = operations.checksum;
    operations.checksum = [&](const auto&, const auto&, const auto&) { cancelled = true; return false; };
    check(run() == gog::InstallError::none && fs::exists(folder / L"setup.exe") && downloads == 4, "cancelled verification does not delete ready files");
    cancelled = false; operations.checksum = checksum; installer_fails = false;
    check(run() == gog::InstallError::none && downloads == 5 && executions == 4, "clean retry completes after corruption and cancellation");
    check(fs::exists(folder / L"unrelated.txt") && !fs::exists(folder / L"setup.exe") && !fs::exists(folder / L"data.bin"), "repair and cleanup preserve unrelated files");
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-installer-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    try { parsing(); lifecycle(root); repairCachedParts(root); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "GOG offline installer contracts, integrity, cancellation, resume and owned-file cleanup passed\n";
}
