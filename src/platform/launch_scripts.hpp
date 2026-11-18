#pragma once
// A game's own scripts (Opcje uruchamiania → Skrypty): commands run before the game starts (waited
// for), after it has started and after it has exited. Whatever the user types runs as it is: on
// Windows through Windows PowerShell (paths to .bat, .cmd, .exe, .ps1, .vbs with their arguments,
// Start-Process, several commands joined with ';'), on Linux through /bin/sh -c. An empty command
// runs nothing. Hidden (no console window); its output (stdout and stderr together) is kept.
#include <chrono>
#include <functional>
#include <string>
#include <string_view>

namespace launch_scripts {
enum class Phase { before, started, exited };
inline constexpr int phase_count = 3;
// "before", "started", "exited": LAUNCHME_SCRIPT_PHASE and the game log.
const char* phaseName(Phase phase);

struct Context {
    Phase phase = Phase::before;
    std::wstring game_name; // LAUNCHME_GAME_NAME
    std::string game_key;   // LAUNCHME_GAME_KEY
};
struct Result {
    bool started = false;  // The shell could be started.
    bool finished = false; // It ended within the time given (else it is left running).
    int exit_code = -1;    // When finished.
    std::string output;    // UTF-8 (Windows: PowerShell is asked for UTF-8), the last 64 KiB.
    bool ok() const { return started && finished && exit_code == 0; }
};

// Pure: what PowerShell runs for a typed command. A command that begins with a quoted path, or with an
// unquoted absolute path with spaces up to a program's or script's extension, gets PowerShell's call
// operator (& 'C:\Program Files\Tool\run.bat' -x); the exit code of a failed native program is kept.
std::wstring powershellScript(std::wstring_view command);
// Pure: -EncodedCommand's argument (UTF-16LE in Base64).
std::wstring encodedCommand(std::wstring_view script);
// Pure: the output's last non-empty line, at most 160 characters (the test's note, the toast).
std::wstring lastLine(std::string_view output);

// Runs `command` and waits at most `timeout` (the worker's thread only); `cancel` stops the waiting
// (never the script). Started in the user's home folder with LAUNCHME_GAME_NAME, LAUNCHME_GAME_KEY and
// LAUNCHME_SCRIPT_PHASE set. An empty command: started == false.
Result run(std::wstring_view command, const Context& context, std::chrono::milliseconds timeout,
           const std::function<bool()>& cancel = {});
}
