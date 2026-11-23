#pragma once
// Installing and removing a game's mods (game_mods.hpp): each mod's newest release is downloaded once
// into <data>\cache, its files placed into the game's folder and written to the folder's record. On the
// worker's thread; every outside effect goes through Services (tests replace them).
//
// The mods depend on each other: RenoDX and DLSS 5 are ReShade add-ons (ReShade goes in first, as
// Request::reshade_method; removing ReShade removes them), ReShade beside OptiScaler or ANR FG's full
// package goes in as ReShade64.dll / ReShade32.dll, which OptiScaler loads itself (LoadReshade=true),
// ANR FG's add-on needs an OptiScaler, and its full package brings one of its own.
#include "game_mods.hpp"
#include "../net/http.hpp"
#include "../platform/launch_scripts.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace game_mods {
struct Services {
    // A small text (a page, a release's JSON, a manifest).
    std::function<HttpResponse(const std::wstring& url)> fetch;
    // A large file to `file` (complete or false).
    std::function<bool(const std::wstring& url, const std::filesystem::path& file)> download;
    // A .7z's contents into `folder` (Windows: tar.exe).
    std::function<bool(const std::filesystem::path& archive, const std::filesystem::path& folder)> extract7z;
    // A PowerShell command (ANR FG's own install.ps1 / uninstall.ps1).
    std::function<launch_scripts::Result(std::wstring_view command)> shell;
    std::function<void(std::wstring_view)> progress; // "Pobieranie OptiScaler v0.9.5…"
    std::function<bool()> cancelled;
};

struct Request {
    Mod mod = Mod::reshade;
    std::string method;            // Empty: the mod's default (game_mods::methods).
    bool remove = false;
    Target target;
    std::string reshade_method;    // ReShade's file when RenoDX or DLSS 5 bring it.
    std::string optiscaler_method; // ANR FG's full package: OptiScaler's file (dxgi.dll, winmm.dll or version.dll).
    bool allow_anticheat = false;  // Asked once: the game has anti-cheat.
};

enum class Problem { none, failed, anticheat };
struct Outcome {
    Problem problem = Problem::failed;
    std::wstring message;          // What happened (the panel's note, a toast).
    bool ok() const { return problem == Problem::none; }
};

// Installs (or updates, or changes the method of) one mod, or removes it.
Outcome apply(const Request& request, const std::filesystem::path& data, const Services& services);

// Pure: PowerShell's single-quoted string.
std::wstring quotePowerShell(std::wstring_view text);
// A ZIP's every safe entry into `folder` (an .exe with a ZIP appended too).
bool extractZip(const std::filesystem::path& archive, const std::filesystem::path& folder);
// The default services: WinHTTP, tar.exe, Windows PowerShell.
Services defaultServices(std::function<bool()> cancelled, std::function<void(std::wstring_view)> progress);
} // namespace game_mods
