// The taskbar's Jump List: Ostatnio grane (a click starts the game) and two tasks, Ustawienia and Tryb
// TV. Each entry runs launchme.exe --jump=<command>; a running launcher receives it from that second
// process (main.cpp, WM_COPYDATA), a starting one from its own arguments.
#include "launcher.hpp"
#include "../platform/app_identity.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <objectarray.h>
#include <propsys.h>
#include <shobjidl.h>
#endif

#include <algorithm>

#ifdef _WIN32
using Microsoft::WRL::ComPtr;
#endif

namespace {
#ifdef _WIN32
// PKEY_Title (FMTID_SummaryInformation, 2): the entry's text.
constexpr PROPERTYKEY title_key{{0xF29F85E0, 0x4FF9, 0x1068, {0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9}}, 2};
constexpr size_t recent_count = 6;

std::wstring hexOf(const std::string& bytes) {
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring text;
    for (const unsigned char c : bytes) { text += digits[c >> 4]; text += digits[c & 15]; }
    return text;
}

#endif

std::string bytesOf(std::wstring_view hex) {
    std::string bytes;
    const auto value = [](wchar_t c) { return c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1; };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int high = value(hex[i]), low = value(hex[i + 1]);
        if (high < 0 || low < 0) return {};
        bytes += static_cast<char>(high * 16 + low);
    }
    return bytes;
}

#ifdef _WIN32
ComPtr<IShellLinkW> link(const std::wstring& exe, const std::wstring& command, const std::wstring& title) {
    ComPtr<IShellLinkW> shell;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(shell.GetAddressOf())))) return {};
    shell->SetPath(exe.c_str());
    shell->SetArguments((L"--jump=" + command).c_str());
    shell->SetIconLocation(exe.c_str(), 0);
    shell->SetDescription(title.c_str());
    ComPtr<IPropertyStore> store;
    if (FAILED(shell.As(&store))) return {};
    PROPVARIANT value{};
    value.vt = VT_LPWSTR;
    value.pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc((title.size() + 1) * sizeof(wchar_t)));
    if (!value.pwszVal) return {};
    std::copy(title.c_str(), title.c_str() + title.size() + 1, value.pwszVal);
    const bool titled = SUCCEEDED(store->SetValue(title_key, value)) && SUCCEEDED(store->Commit());
    PropVariantClear(&value);
    return titled ? shell : ComPtr<IShellLinkW>{};
}
#endif
} // namespace

void Launcher::jumpCommand(const std::wstring& command) {
    if (command.empty()) return;
    jump_pending_ = command;
    jump_pending_since_ = GetTickCount64();
    pollJumpList();
}

// A "play:" game's session ended (or it never started: not found, not launched in 15 s).
std::vector<std::string> Launcher::finishedPlays() {
    std::vector<std::string> finished;
    if (play_waits_.empty()) return finished;
    const auto now = GetTickCount64();
    for (auto wait = play_waits_.begin(); wait != play_waits_.end();) {
        const bool running = game_sessions_.contains(wait->key);
        if (running) wait->seen = true;
        const bool starting = (pending_launch_ && gameKey(pending_game_) == wait->key) || launchAnimationActive();
        if ((wait->seen && !running) || (!wait->seen && !running && !starting && now - wait->since > 15000)) {
            finished.push_back(text_encoding::utf8Lenient(wait->command));
            wait = play_waits_.erase(wait);
        } else ++wait;
    }
    return finished;
}

void Launcher::pollJumpList() {
    if (!hwnd_ || closing_) return;
    const auto now = GetTickCount64();
    // A waiting command: the tasks at once, a game once the library holds it (or give up after a minute).
    if (!jump_pending_.empty() && !launchAnimationActive()) {
        const auto command = jump_pending_;
        if (command == L"settings") { jump_pending_.clear(); if (!settingsOpen()) openSettings(); }
        else if (command == L"tv") { jump_pending_.clear(); if (desktop_mode_) setDesktopMode(false); }
        else if (command.starts_with(L"launch:") || command.starts_with(L"play:")) {
            const bool play = command.starts_with(L"play:");
            const auto key = bytesOf(std::wstring_view(command).substr(play ? 5 : 7));
            const Game* found = nullptr;
            for (const auto* games : {&public_library_, &private_category_.games})
                for (const auto& game : *games) if (!found && gameKey(game) == key) found = &game;
            if (found) {
                jump_pending_.clear();
                const auto game = *found;
                if (play) play_waits_.push_back({command, key, now, false});
                launchGame(game);
            } else if (key.empty() || now - jump_pending_since_ > 60000) {
                jump_pending_.clear();
                if (play) play_waits_.push_back({command, key, 0, false}); // Its sender stops waiting.
            }
        } else jump_pending_.clear();
    }
#ifndef _WIN32
    // No taskbar Jump List on Linux: commands still arrive from a second start (--jump=, main.cpp).
    (void)now;
#else
    // The list itself: rebuilt only when the recently played games change.
    if (now - jump_list_checked_ < 10000 || grouped_library_.empty()) return;
    jump_list_checked_ = now;
    std::vector<const Game*> recent;
    for (const auto& game : grouped_library_)
        if (isGame(game) && game.last_played && game.visibility == GameVisibility::public_game) recent.push_back(&game);
    std::sort(recent.begin(), recent.end(), [](const Game* a, const Game* b) { return a->last_played > b->last_played; });
    if (recent.size() > recent_count) recent.resize(recent_count);
    std::string signature; // Names in the interface language: a change of it shows below.
    for (const auto* game : recent) signature += '\n' + gameKey(*game) + '\t' + text_encoding::utf8(game->name);
    signature += '\n' + text_encoding::utf8(tr(L"Ustawienia"));
    if (signature == jump_list_signature_) return;
    jump_list_signature_ = signature;
    wchar_t path[MAX_PATH * 2]{};
    if (!GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)))) return;
    const std::wstring exe = path;
    ComPtr<ICustomDestinationList> list;
    if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(list.GetAddressOf())))) return;
    list->SetAppID(app_identity::id);
    UINT slots = 0;
    ComPtr<IObjectArray> removed;
    if (FAILED(list->BeginList(&slots, IID_PPV_ARGS(removed.GetAddressOf())))) return;
    const auto collection = [] {
        ComPtr<IObjectCollection> items;
        CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(items.GetAddressOf()));
        return items;
    };
    if (auto games = collection()) {
        for (const auto* game : recent) if (auto item = link(exe, L"launch:" + hexOf(gameKey(*game)), game->name)) games->AddObject(item.Get());
        ComPtr<IObjectArray> array;
        UINT count = 0;
        // Refused when the user turned recent items off, or removed one of these: the tasks still go in.
        if (SUCCEEDED(games.As(&array)) && SUCCEEDED(array->GetCount(&count)) && count) list->AppendCategory(tr(L"Ostatnio grane"), array.Get());
    }
    if (auto tasks = collection()) {
        if (auto item = link(exe, L"settings", tr(L"Ustawienia"))) tasks->AddObject(item.Get());
        if (auto item = link(exe, L"tv", tr(L"Tryb TV"))) tasks->AddObject(item.Get());
        ComPtr<IObjectArray> array;
        if (SUCCEEDED(tasks.As(&array))) list->AddUserTasks(array.Get());
    }
    if (FAILED(list->CommitList())) jump_list_signature_.clear(); // Tried again later.
#endif
}
