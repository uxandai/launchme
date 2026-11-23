#include "../src/plugins/manager.hpp"
#include "../src/plugins/package.hpp"
#include "../src/platform/atomic_file.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>
#include <iostream>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F fn, const char* message) { bool rejected = false; try { fn(); } catch (const std::exception&) { rejected = true; } check(rejected, message); }
std::string manifest(std::string version = "1.0.0") {
    return R"({"apiVersion":1,"architecture":"windows-x64","id":"test.lifecycle","version":")" + version + R"(","entry":"plugin.dll","name":"Lifecycle",
      "settings":[{"id":"secret","type":"secret","title":"Key","default":""},{"id":"switch","type":"boolean","title":"Switch","default":false},
      {"id":"mode","type":"choice","title":"Mode","default":"one","choices":[{"value":"one","title":"One"},{"value":"two","title":"Two"}]}],
      "contributions":[{"id":"echo","kind":"menu","title":"Echo","command":"echo"},{"id":"broken","kind":"context","source":"steam","title":"Broken","command":"broken"}]})";
}
void zip(const std::filesystem::path& file, const std::vector<std::pair<std::string, std::string>>& files) {
    mz_zip_archive archive{}; check(mz_zip_writer_init_heap(&archive, 0, 0) != 0, "zip writer");
    std::vector<std::pair<std::string, std::string>> replacements;
    for (const auto& [name, data] : files) {
        auto stored = name;
        if (!plugins::packagePath(name)) { stored.assign(name.size(), 'x'); replacements.emplace_back(stored, name); }
        check(mz_zip_writer_add_mem(&archive, stored.c_str(), data.data(), data.size(), MZ_BEST_COMPRESSION) != 0, "zip entry");
    }
    void* bytes = nullptr; size_t count = 0;
    check(mz_zip_writer_finalize_heap_archive(&archive, &bytes, &count) != 0, "zip finalize");
    std::string raw(static_cast<const char*>(bytes), count);
    for (const auto& [from, to] : replacements)
        for (size_t at = 0; (at = raw.find(from, at)) != raw.npos; at += from.size()) raw.replace(at, from.size(), to);
    check(atomic_file::write(file, raw), "zip write");
    mz_free(bytes); mz_zip_writer_end(&archive);
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace plugins; using namespace plugins::wire;
    if (argc != 3) return 1;
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-plugin-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    try {
        auto icon_manifest = *json::parse(manifest());
        icon_manifest.properties["icon"] = string("images/icon.png");
        icon_manifest.properties["iconText"] = string("EX");
        check(parseManifest(encode(icon_manifest)).icon_text == L"EX", "plugin monogram");
        for (const auto* invalid : {"../icon.png", "C:/icon.png", "image.svg", "nul.png", ""}) {
            icon_manifest.properties["icon"] = string(invalid);
            rejects([&] { parseManifest(encode(icon_manifest)); }, "invalid icon path accepted");
        }
        icon_manifest.properties.erase("icon"); icon_manifest.properties["iconText"] = string("TOO LONG");
        rejects([&] { parseManifest(encode(icon_manifest)); }, "invalid icon text accepted");
        {
            const auto base = R"({"apiVersion":1,"architecture":"windows-x64","id":"test.launch","version":"1.0.0","entry":"plugin.dll","name":"Launch",)";
            const auto parsed = parseManifest(std::string(base) +
                R"("contributions":[{"id":"run","kind":"launch","source":"steam","title":"Run","command":"run"}]})");
            check(parsed.contributions.size() == 1 && parsed.contributions[0].kind == "launch", "launch contribution parsed");
            Game steam{620, L"Portal 2"}; Game gog = steam; gog.source = GameSource::gog; gog.kind = GameKind::nonsteam;
            check(matches(parsed.contributions[0], steam) && !matches(parsed.contributions[0], gog), "launch provider source filtering");
            rejects([&] { parseManifest(std::string(base) +
                R"("contributions":[{"id":"run","kind":"launch","title":"Run","command":"run"}]})"); }, "launch provider without source accepted");
            rejects([&] { parseManifest(std::string(base) +
                R"("contributions":[{"id":"a","kind":"launch","source":"steam","title":"A","command":"a"},{"id":"b","kind":"launch","source":"gog","title":"B","command":"b"}]})"); }, "two launch providers accepted");
        }
        const auto dll = readFile(argv[1], 32 * 1024 * 1024), bad_dll = readFile(argv[2], 32 * 1024 * 1024);
        const auto package = root / L"test.zip", broken = root / L"bad.zip", profile = root / L"profile";
        const auto lifecycle = profile / L"data/test.lifecycle/lifecycle.txt";
        zip(package, {{"plugin.json", manifest()}, {"plugin.dll", dll}});
        auto params = object(); params.properties["command"] = string("echo");
        {
            Manager manager(profile); manager.load();
            check(manager.install(package) == "test.lifecycle", "installed ID");
            check(!manager.snapshot().plugins[0].enabled && !std::filesystem::exists(lifecycle), "install must not execute disabled DLL");
            rejects([&] { manager.invoke("test.lifecycle", "command", params, "en"); }, "disabled invocation");
            manager.setting("test.lifecycle", "secret", string("do-not-store-in-plaintext"));
            manager.setting("test.lifecycle", "switch", plugins::wire::boolean(true));
            manager.setting("test.lifecycle", "mode", string("two"));
            rejects([&] { manager.setting("test.lifecycle", "mode", string("unknown")); }, "choice validation");
            check(readFile(profile / L"registry.dat", 1024 * 1024).find("do-not-store-in-plaintext") == std::string::npos, "encrypted registry");
            check(text(manager.snapshot().plugins[0].settings, "secret") == "set", "redacted snapshot");
            manager.enable("test.lifecycle", true);
            check(readFile(lifecycle, 4096) == "activate\n", "activation once");
            check(manager.snapshot().contributions("menu").size() == 1, "menu registration");
            Game steam{620, L"Portal 2"}; Game shortcut = steam; shortcut.kind = GameKind::nonsteam;
            check(manager.snapshot().contributions("context", &steam).size() == 1 && manager.snapshot().contributions("context", &shortcut).empty(), "context filtering");
            check(text(field(manager.invoke("test.lifecycle", "command", params, "en"), "settings"), "secret") == "do-not-store-in-plaintext", "settings delivered over ABI");
            manager.enable("test.lifecycle", false);
            check(readFile(lifecycle, 4096) == "activate\ndeactivate\n", "deactivation once");
            check(manager.snapshot().contributions("menu").empty(), "unregister contributions");
        }
        {
            Manager manager(profile); manager.load();
            check(!manager.snapshot().plugins[0].enabled && readFile(lifecycle, 4096) == "activate\ndeactivate\n", "disabled after restart, DLL not called");
            manager.enable("test.lifecycle", true);
            zip(package, {{"plugin.json", manifest("1.1.0")}, {"plugin.dll", dll}});
            manager.install(package);
            check(manager.snapshot().plugins[0].enabled && manager.snapshot().plugins[0].manifest.version == "1.1.0", "live update");
            check(text(field(manager.invoke("test.lifecycle", "command", params, "pl"), "settings"), "mode") == "two", "update retains settings");
            zip(broken, {{"plugin.json", manifest("2.0.0")}, {"plugin.dll", bad_dll}});
            rejects([&] { manager.install(broken); }, "ABI mismatch on live update");
            check(manager.snapshot().plugins[0].enabled && manager.snapshot().plugins[0].manifest.version == "1.1.0", "failed update rolled back");
            for (const auto* path : {"../escape.dll", "a/../../escape.dll", "/absolute.dll", "C:/escape.dll", "a\\escape.dll", "file:stream", "nul.txt", "con.dll", "a./x"}) {
                zip(broken, {{"plugin.json", manifest()}, {"plugin.dll", dll}, {path, "escape"}});
                rejects([&] { manager.install(broken); }, "unsafe ZIP path accepted");
            }
            zip(broken, {{"plugin.json", manifest()}, {"plugin.dll", dll}, {"PLUGIN.DLL", "duplicate"}});
            rejects([&] { manager.install(broken); }, "case alias accepted");
            zip(broken, {{"plugin.json", "{}"}, {"plugin.dll", dll}});
            rejects([&] { manager.install(broken); }, "invalid manifest accepted");
            auto unsupported = manifest(); unsupported.replace(unsupported.find("\"apiVersion\":1"), 14, "\"apiVersion\":2");
            zip(broken, {{"plugin.json", unsupported}, {"plugin.dll", dll}});
            rejects([&] { manager.install(broken); }, "unsupported API accepted");
            zip(broken, {{"plugin.json", manifest()}, {"plugin.dll", std::string(100, '\0')}});
            rejects([&] { manager.install(broken); }, "invalid DLL accepted");
            atomic_file::write(broken, "not a zip");
            rejects([&] { manager.install(broken); }, "corrupt archive accepted");
            // Registry write failure: current module/state survives a rejected disable.
            std::filesystem::rename(profile / L"registry.dat", profile / L"saved.dat");
            std::filesystem::create_directory(profile / L"registry.dat");
            rejects([&] { manager.enable("test.lifecycle", false); }, "write failure not reported");
            check(manager.snapshot().plugins[0].enabled, "failed persistence changed live state");
            std::filesystem::remove(profile / L"registry.dat"); std::filesystem::rename(profile / L"saved.dat", profile / L"registry.dat");
        }
        {
            Manager manager(profile); manager.load();
            check(manager.snapshot().plugins[0].enabled, "enabled after restart");
            params.properties["command"] = string("broken");
            rejects([&] { manager.invoke("test.lifecycle", "command", params, "en"); }, "bad response accepted");
            check(!manager.snapshot().plugins[0].enabled && !manager.snapshot().plugins[0].error.empty(), "fault isolation");
        }
        {
            Manager manager(profile); manager.load();
            check(!manager.snapshot().plugins[0].enabled, "fault persisted");
            manager.enable("test.lifecycle", true); manager.remove("test.lifecycle");
            check(manager.snapshot().plugins.empty() && !std::filesystem::exists(profile / L"packages/test.lifecycle"), "uninstall unloads DLL and removes package");
        }
        {
            Manager manager(profile); manager.load(); check(manager.snapshot().plugins.empty(), "uninstall survives restart");
            manager.install(package); check(!manager.snapshot().plugins[0].enabled, "reinstall starts disabled");
            check(text(manager.snapshot().plugins[0].settings, "secret").empty(), "uninstall removed settings");
            manager.remove("test.lifecycle");
        }
        const auto bundled_profile = root / L"bundled";
        {
            Manager manager(bundled_profile); manager.load(); manager.ensureBundled("test.lifecycle", package);
            check(manager.snapshot().plugins.size() == 1 && manager.snapshot().plugins[0].enabled, "bundled plugin defaults on");
            manager.enable("test.lifecycle", false);
        }
        {
            Manager manager(bundled_profile); manager.load(); manager.ensureBundled("test.lifecycle", package);
            check(!manager.snapshot().plugins[0].enabled, "bundled plugin respects disabled preference after restart");
            zip(package, {{"plugin.json", manifest("1.2.0")}, {"plugin.dll", dll}});
            manager.ensureBundled("test.lifecycle", package);
            check(!manager.snapshot().plugins[0].enabled && manager.snapshot().plugins[0].manifest.version == "1.2.0", "bundled update preserves disabled preference");
            manager.remove("test.lifecycle");
        }
        {
            Manager manager(bundled_profile); manager.load(); manager.ensureBundled("test.lifecycle", package);
            check(manager.snapshot().plugins.empty(), "bundled plugin respects uninstall after restart");
        }
        std::cout << "Plugin ZIP, ABI, lifecycle, settings, update rollback, restart and uninstall passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
