#pragma once

#include "../model/game.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

// Discord tile of the drawer dashboard: starts or focuses the desktop client only.
// No Discord API or RPC client. Safe to call from any thread.
namespace discord {
// program: a client chosen by the player (Vesktop, Legcord…: Ustawienia → System → Discord w Widżetach),
// known by its file name; empty: Discord's own (stable, PTB, Canary).
// True while that client's process runs. On Linux also when a client listens on its IPC socket
// (a sandboxed launcher cannot see other processes).
bool running(const std::filesystem::path& program = {});
// Brings a visible top-level window of that client to the foreground; false when there is
// none (not running, or minimised to the tray). Always false on Linux (no window
// activation from other apps on Wayland): starting the client again shows its window.
bool focus(const std::filesystem::path& program = {});
// How to start the client: its Squirrel Update.exe --processStart under %LOCALAPPDATA%
// (Discord, DiscordPTB, DiscordCanary), else the registered discord:// protocol;
// empty when Discord is not installed. On Linux: discord on PATH or in /opt, the Flatpak
// or Snap, else a desktop file handling discord://.
std::optional<GameAction> launchAction();

// Linux, for tests: a Discord client's process name (/proc/<pid>/comm), and whether a client
// listens on discord-ipc-0..9 under `runtime` ($XDG_RUNTIME_DIR; also its Flatpak and Snap
// subfolders app/com.discordapp.Discord, app/com.discordapp.DiscordCanary and snap.discord).
bool clientProcessName(std::string_view comm);
bool ipcListening(const std::filesystem::path& runtime);
} // namespace discord
