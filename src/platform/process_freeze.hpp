#pragma once
#include "../model/frozen_games.hpp"

#include <filesystem>
#include <optional>
#include <vector>

// Experimental quick resume. A game is suspended in RAM (every thread of its
// processes via NtSuspendProcess, as in Resource Monitor) and resumed later.
// Nothing is dumped: Windows may page an idle game out under memory pressure.
// Linux: SIGSTOP/SIGCONT/SIGKILL on the processes and, when suspending, every process
// below them; identity is the PID with its start time (/proc/<pid>/stat). focus() is false.
namespace process_freeze {
enum class Result { ok, denied, gone };
// All or nothing. On success `processes` keeps only the ones actually suspended;
// a denied process (elevated, protected, anti-cheat) rolls the others back.
Result suspend(std::vector<GameProcess>& processes);
// Best effort: exited or reused PIDs are skipped. `gone` = nothing left to resume.
// Resuming a thread that is not suspended is a no-op, so a retry is safe.
Result resume(const std::vector<GameProcess>& processes);
// Ends the exact processes, suspended or not (like quitting on a console).
// `denied` if any of them refused; `gone` if all had already exited.
Result terminate(const std::vector<GameProcess>& processes);
// Conservative: a process that cannot be inspected counts as alive.
bool alive(const std::vector<GameProcess>& processes);
// Restores and activates the largest visible top-level window of the processes.
bool focus(const std::vector<GameProcess>& processes);
// Brings a window to the foreground even when this process is not in front (Windows; false elsewhere).
bool bringForward(void* window); // An HWND.

// Missing file = empty list; unreadable/invalid file = nullopt, never an empty journal.
std::optional<std::vector<FrozenGame>> load(const std::filesystem::path& file);
// An empty list removes the file.
bool save(const std::filesystem::path& file, const std::vector<FrozenGame>& games);
}
