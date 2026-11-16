// A Steam game's own Proton settings: Steam's files (read, written, backed up, the rest kept),
// the launch-option switches, the queue of changes waiting for Steam to close and the live
// expressions' replies. Never this machine's Steam: every file is a fixture in a temp folder.
#include "../src/library/steam_game_config.hpp"
#include "../src/library/steam_registry.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace steam_game_config;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::cerr << "FAILED: " << what << '\n'; ++failures; }
}
std::string readFile(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
void writeFile(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}
const Switch& named(std::string_view id) {
    for (const auto& item : switches()) if (item.id == id) return item;
    std::abort();
}

void files(const fs::path& root) {
    const auto steam = root / "steam";
    check(localConfig(steam, 76561198000000123ULL) == steam / "userdata" / "39734395" / "config" / "localconfig.vdf", "SteamID64 → userdata folder");
    check(localConfig(steam, 39734395) == localConfig(steam, 76561198000000123ULL) && localConfig(steam, 0).empty(), "the 32-bit ID too; none without one");
    const auto local = localConfig(steam, 39734395), install = installConfig(steam);
    writeFile(local, "\"UserLocalConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n"
                     "\t\t\t\t\"apps\"\n\t\t\t\t{\n\t\t\t\t\t\"570\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LaunchOptions\"\t\t\"-novid \\\"x y\\\"\"\n"
                     "\t\t\t\t\t\t\"LastPlayed\"\t\t\"1700000000\"\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t\t\"other\"\t\t\"kept\"\n\t\t\t}\n\t\t}\n\t}\n}\n");
    writeFile(install, "\"InstallConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n"
                       "\t\t\t\t\"CompatToolMapping\"\n\t\t\t\t{\n\t\t\t\t\t\"0\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"name\"\t\t\"proton_9\"\n"
                       "\t\t\t\t\t\t\"config\"\t\t\"\"\n\t\t\t\t\t\t\"priority\"\t\t\"75\"\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n");
    const auto first = read(local, install, 570);
    check(first && first->launch_options == "-novid \"x y\"" && first->compat_tool.empty(), "the game's options; the global default is not its tool");
    const auto other = read(local, install, 440);
    check(other && other->launch_options.empty() && other->compat_tool.empty(), "a game Steam sets nothing for");

    check(write(local, install, 570, std::string("PROTON_ENABLE_NVAPI=1 %command% -novid"), std::string("GE-Proton10-4")), "written");
    const auto second = read(local, install, 570);
    check(second && second->launch_options == "PROTON_ENABLE_NVAPI=1 %command% -novid" && second->compat_tool == "GE-Proton10-4", "read back");
    auto backup = local; backup += ".launchme-backup";
    check(readFile(backup).find("-novid \\\"x y\\\"") != std::string::npos, "the file as it was, backed up");
    const auto text = readFile(local);
    check(text.find("\"LastPlayed\"\t\t\"1700000000\"") != std::string::npos && text.find("\"other\"\t\t\"kept\"") != std::string::npos, "everything else kept");
    const auto installed = readFile(install);
    check(installed.find("\"570\"") != std::string::npos && installed.find("\"priority\"\t\t\"250\"") != std::string::npos &&
          installed.find("\"proton_9\"") != std::string::npos, "the game's mapping beside the default");

    check(write(local, install, 440, std::nullopt, std::string("proton_experimental")) && read(local, install, 440)->compat_tool == "proton_experimental",
          "a tool for a game without an entry");
    check(write(local, install, 570, std::nullopt, std::string()) && read(local, install, 570)->compat_tool.empty() &&
          readFile(install).find("\"proton_9\"") != std::string::npos, "an empty tool: Steam's default again, the default kept");
    check(read(local, install, 570)->launch_options == "PROTON_ENABLE_NVAPI=1 %command% -novid", "a tool change leaves the options alone");
    check(write(local, install, 730, std::string("-high\tx\nnext"), std::nullopt) && read(local, install, 730)->launch_options == "-high\tx\nnext",
          "escapes round-trip");

    // A fresh account: the files are made.
    const auto fresh = localConfig(steam, 7);
    check(write(fresh, install, 10, std::string("-x"), std::nullopt) && read(fresh, install, 10)->launch_options == "-x", "a missing localconfig is created");
    // A damaged file is never overwritten.
    const auto broken = root / "broken.vdf";
    writeFile(broken, "\"UserLocalConfigStore\"\n{\n\"apps\"\n{\n");
    check(!read(broken, install, 570) && !write(broken, install, 570, std::string("-x"), std::nullopt) &&
          readFile(broken) == "\"UserLocalConfigStore\"\n{\n\"apps\"\n{\n", "unreadable: left as it is");
    check(cefFlag(steam) == steam / ".cef-enable-remote-debugging" && cefFlag({}).empty(), "the CEF flag's place");
}

void names(const fs::path& root) {
    const auto custom = root / "compatibilitytools.d" / "GE-Proton10-4";
    writeFile(custom / "compatibilitytool.vdf", "\"compatibilitytools\"\n{\n  \"compat_tools\"\n  {\n    \"GE-Proton10-4\"\n    {\n"
                                                "      \"display_name\" \"GE-Proton10-4\"\n    }\n  }\n}\n");
    check(internalName(custom) == "GE-Proton10-4", "compatibilitytool.vdf's key");
    const auto common = root / "common";
    for (const auto* folder : {"Proton 9.0", "Proton 6.3", "Proton - Experimental", "Proton Hotfix", "Proton 10.0", "Steam Linux Runtime 3.0 (sniper)"})
        fs::create_directories(common / folder);
    check(internalName(common / "Proton 9.0") == "proton_9" && internalName(common / "Proton 6.3") == "proton_63" &&
          internalName(common / "Proton 10.0") == "proton_10", "Valve's Proton by version");
    check(internalName(common / "Proton - Experimental") == "proton_experimental" && internalName(common / "Proton Hotfix") == "proton_hotfix",
          "Experimental and Hotfix");
    check(internalName(common / "Steam Linux Runtime 3.0 (sniper)").empty(), "not a Proton");
}

void composing() {
    const auto& nvapi = named("nvapi");
    const auto& mangohud = named("mangohud");
    const auto& gamemode = named("gamemode");
    const auto& dxr = named("dxr");
    check(toggle("", nvapi, true) == "PROTON_ENABLE_NVAPI=1 %command%", "on an empty line");
    check(toggle("-novid -high", nvapi, true) == "PROTON_ENABLE_NVAPI=1 %command% -novid -high", "the game's arguments stay after %command%");
    check(toggle("PROTON_ENABLE_NVAPI=1 %command% -novid", nvapi, false) == "-novid", "off: only the arguments left");
    check(toggle("PROTON_ENABLE_NVAPI=1 %command%", nvapi, false).empty(), "off: nothing left");
    auto line = toggle("DXVK_ASYNC=1 gamemoderun %command% -dx11", mangohud, true);
    check(line == "DXVK_ASYNC=1 gamemoderun mangohud %command% -dx11", "a wrapper just before %command%");
    line = toggle(line, nvapi, true);
    check(line == "DXVK_ASYNC=1 PROTON_ENABLE_NVAPI=1 gamemoderun mangohud %command% -dx11", "a variable after the others, before wrappers");
    check(has(line, nvapi) && has(line, mangohud) && has(line, gamemode) && !has(line, dxr), "what is on");
    check(toggle("VKD3D_CONFIG=dxr11 %command%", dxr, true) == "VKD3D_CONFIG=dxr %command%" && !has("VKD3D_CONFIG=dxr11 %command%", dxr),
          "another value of the same variable is replaced");
    check(toggle("WINEDLLOVERRIDES=\"a b=n\" %command%", nvapi, true) == "WINEDLLOVERRIDES=\"a b=n\" PROTON_ENABLE_NVAPI=1 %command%", "quoted words stay whole");
    check(!has("-mangohud", mangohud) && !has("mangohud", mangohud), "words after %command% (or without it) are the game's");
    check(toggle(toggle("", gamemode, true), gamemode, true) == "gamemoderun %command%", "never twice");
}

void pending(const fs::path& root) {
    const auto file = root / "pending.txt";
    std::vector<Pending> list;
    queue(list, {570, std::string("PROTON_LOG=1 %command%"), std::nullopt});
    queue(list, {570, std::nullopt, std::string("GE-Proton10-4")});
    queue(list, {440, std::string("weird \"text\"\nline"), std::nullopt});
    queue(list, {570, std::string("-novid"), std::nullopt});
    queue(list, {0, std::string("x"), std::nullopt});
    check(list.size() == 2 && list[0].launch_options == "-novid" && list[0].compat_tool == "GE-Proton10-4", "one entry per game, the later change wins");
    check(savePending(file, list) && loadPending(file) == list, "kept across a restart");
    std::ofstream(file, std::ios::app) << "garbage line\n12 launch zz\n";
    check(loadPending(file) == list, "damaged lines skipped");
    check(savePending(file, {}) && !fs::exists(file) && loadPending(file).empty(), "nothing waits: no file");
}

void live() {
    const auto read = readExpression(570);
    check(read.find("RegisterForAppDetails(app") != std::string::npos && read.ends_with("(570)"), "the read expression");
    const auto write = writeExpression(570, std::string("A=\"1\" %command% \\x\n</script>"), std::string("GE-Proton10-4"));
    check(write.find("SetAppLaunchOptions(570, \"A=\\\"1\\\" %command% \\\\x\\u000a</script>\")") != std::string::npos &&
          write.find("SpecifyCompatTool(570, \"GE-Proton10-4\")") != std::string::npos, "values as escaped literals");
    check(writeExpression(570, std::nullopt, std::string()).find("SetAppLaunchOptions") == std::string::npos &&
          writeExpression(570, std::nullopt, std::string()).find("SpecifyCompatTool(570, \"\")") != std::string::npos, "only what changes");
    const auto reply = parseReply(R"({"launch":"PROTON_LOG=1 %command%","tool":"proton_9","tools":[{"name":"proton_9","label":"Proton 9.0-4"},{"name":"","label":"x"},{"name":"GE-Proton10-4"}]})");
    check(reply && reply->launch_options == "PROTON_LOG=1 %command%" && reply->compat_tool == "proton_9" && reply->tools.size() == 2 &&
          reply->tools[0].label == L"Proton 9.0-4" && reply->tools[1].label == L"GE-Proton10-4", "a reply with Steam's tools");
    check(!parseReply("[]") && !parseReply(R"({"launch":1,"tool":""})") && !parseReply("nope"), "not a reply");
}
}

int main() {
    const auto root = fs::temp_directory_path() / ("launchme-steam-game-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    files(root);
    names(root);
    composing();
    pending(root);
    live();
    fs::remove_all(root);
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "Steam games' own Proton settings passed\n";
    return 0;
}
