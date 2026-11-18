// Opcje uruchamiania → Skrypty (src/platform/launch_scripts.hpp): what PowerShell is given for a typed
// command, and real runs of the system's shell (PowerShell on Windows, /bin/sh on Linux): output,
// exit codes, the game's variables, a path with spaces, and a script left running past its time.
#include "../src/platform/launch_scripts.hpp"

#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace std::chrono_literals;

int main() {
    using launch_scripts::powershellScript;
    const auto body = [](std::wstring_view command) {
        const auto script = powershellScript(command);
        const auto start = script.find(L"catch {}\n") + 9, end = script.find(L"\n$launchmeOk");
        return script.substr(start, end - start);
    };
    // Typed as is; a quoted path gets the call operator; an unquoted one with spaces is quoted up to its extension.
    if (body(L"  Start-Process notepad  ") != L"Start-Process notepad") return 1;
    if (body(L"\"C:\\Program Files\\Tool\\run.bat\" -x") != L"& \"C:\\Program Files\\Tool\\run.bat\" -x") return 2;
    if (body(L"C:\\Program Files\\Tool\\run.BAT -x \"a b\"") != L"& 'C:\\Program Files\\Tool\\run.BAT' -x \"a b\"") return 3;
    if (body(L"C:\\Tools\\run.bat -x") != L"C:\\Tools\\run.bat -x") return 4; // No spaces: PowerShell runs it itself.
    if (body(L"D:\\Gry\\O'Brien's\\mod tool.exe") != L"& 'D:\\Gry\\O''Brien''s\\mod tool.exe'") return 5;
    if (body(L"\\\\nas\\share\\my scripts\\a.vbs") != L"& '\\\\nas\\share\\my scripts\\a.vbs'") return 6;
    if (body(L"Get-Process | Where-Object { $_.Name -eq 'x' }; C:\\a b.ps1") != L"Get-Process | Where-Object { $_.Name -eq 'x' }; C:\\a b.ps1") return 7;
    if (powershellScript(L"x").find(L"if (-not $launchmeOk) { if ($LASTEXITCODE) { exit $LASTEXITCODE } else { exit 1 } }") == std::wstring::npos) return 8;
    // -EncodedCommand: UTF-16LE in Base64 ("Hi" = 48 00 69 00; "ą" = 05 01).
    if (launch_scripts::encodedCommand(L"Hi") != L"SABpAA==" || launch_scripts::encodedCommand(L"ą") != L"BQE=" ||
        launch_scripts::encodedCommand(L"abc") != L"YQBiAGMA") return 9;
    if (launch_scripts::lastLine("first\r\nsecond line  \r\n\r\n") != L"second line" || !launch_scripts::lastLine("\n \n").empty() ||
        launch_scripts::lastLine("za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87") != L"zażółć" || launch_scripts::lastLine(std::string(300, 'x')).size() != 161) return 10;
    if (std::string(launch_scripts::phaseName(launch_scripts::Phase::before)) != "before" ||
        std::string(launch_scripts::phaseName(launch_scripts::Phase::exited)) != "exited") return 11;

    // Real runs.
    launch_scripts::Context context{launch_scripts::Phase::started, L"Wiedźmin 3", "gog:1207664663"};
    if (launch_scripts::run(L"   ", context, 5s).started) return 20; // An empty command runs nothing.
#ifdef _WIN32
    auto result = launch_scripts::run(L"Write-Output \"$env:LAUNCHME_GAME_NAME|$env:LAUNCHME_GAME_KEY|$env:LAUNCHME_SCRIPT_PHASE\"; exit 3", context, 60s);
#else
    auto result = launch_scripts::run(L"echo \"$LAUNCHME_GAME_NAME|$LAUNCHME_GAME_KEY|$LAUNCHME_SCRIPT_PHASE\"; exit 3", context, 60s);
#endif
    if (!result.started || !result.finished || result.exit_code != 3 || result.ok() ||
        launch_scripts::lastLine(result.output) != L"Wiedźmin 3|gog:1207664663|started") return 21;
    // A program in a folder with spaces, typed without quotes, with its arguments.
    const auto root = std::filesystem::temp_directory_path() / (L"launchme scripts " + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    std::filesystem::create_directories(root);
#ifdef _WIN32
    { std::ofstream(root / L"say it.bat") << "@echo off\r\necho got %1\r\nexit /b 4\r\n"; }
    result = launch_scripts::run((root / L"say it.bat").wstring() + L" hello", context, 60s);
#else
    { std::ofstream(root / "say it.sh") << "#!/bin/sh\necho got $1\nexit 4\n"; }
    std::filesystem::permissions(root / "say it.sh", std::filesystem::perms::owner_all);
    result = launch_scripts::run(L"'" + (root / "say it.sh").wstring() + L"' hello", context, 60s);
#endif
    if (!result.finished || result.exit_code != 4 || launch_scripts::lastLine(result.output) != L"got hello") return 22;
    // A failing command: a non-zero code and its error in the output.
#ifdef _WIN32
    result = launch_scripts::run(L"Get-Item 'C:\\no such folder 7d1f'", context, 60s);
#else
    result = launch_scripts::run(L"ls '/no such folder 7d1f'", context, 60s);
#endif
    if (!result.finished || result.exit_code == 0 || result.output.empty()) return 23;
    // Past its time: no longer waited for (and left running), success after a clean exit.
#ifdef _WIN32
    const auto started = std::chrono::steady_clock::now();
    result = launch_scripts::run(L"Start-Sleep -Seconds 20", context, 1500ms);
    if (!result.started || result.finished || std::chrono::steady_clock::now() - started > 10s) return 24;
    if (!launch_scripts::run(L"$null = 1", context, 60s).ok()) return 25;
#else
    const auto started = std::chrono::steady_clock::now();
    result = launch_scripts::run(L"sleep 20", context, 500ms);
    if (!result.started || result.finished || std::chrono::steady_clock::now() - started > 10s) return 24;
    if (!launch_scripts::run(L"true", context, 60s).ok()) return 25;
#endif
    return 0;
}
