#include "frame_limiter.hpp"
#include "frame_limiter_shared.hpp"

#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <cwctype>
#include <map>
#include <new>
#include <mutex>
#include <string>
#include <thread>

namespace frame_limiter {
namespace {
constexpr const wchar_t* anti_cheat_names[] = {L"easyanticheat", L"beclient", L"beservice", L"battleye", L"vgc", L"xigncode",
    L"gameguard", L"npggnt", L"punkbuster", L"equ8", L"nprotect", L"anticheat", L"anti-cheat", L"faceit", L"ricochet"};

std::wstring lower(std::wstring text) {
    for (auto& c : text) c = static_cast<wchar_t>(std::towlower(c));
    return text;
}

std::filesystem::path processImage(HANDLE process) {
    wchar_t buffer[MAX_PATH * 4]{};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    return QueryFullProcessImageNameW(process, 0, buffer, &size) ? std::filesystem::path(buffer) : std::filesystem::path();
}

bool hasLibrary(HANDLE process) {
    HMODULE modules[2048];
    DWORD needed = 0;
    if (!EnumProcessModulesEx(process, modules, sizeof(modules), &needed, LIST_MODULES_64BIT)) return false;
    for (DWORD i = 0; i < std::min<DWORD>(needed / sizeof(HMODULE), 2048); ++i) {
        wchar_t name[MAX_PATH]{};
        if (GetModuleBaseNameW(process, modules[i], name, MAX_PATH) && lower(name) == library_name) return true;
    }
    return false;
}

// Loads the library with LoadLibraryW on a thread of the game's. kernel32 sits at the same address in
// every 64-bit process of a session.
State inject(DWORD pid, const std::filesystem::path& library) {
    if (!std::filesystem::exists(library)) return State::unsupported;
    const HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) return GetLastError() == ERROR_ACCESS_DENIED ? State::denied : State::failed;
    struct Closer { HANDLE handle; ~Closer() { CloseHandle(handle); } } closer{process};
    USHORT machine = 0, native = 0;
    if (!IsWow64Process2(process, &machine, &native)) return State::failed;
    if (machine != IMAGE_FILE_MACHINE_UNKNOWN) return State::unsupported; // Running under WOW64: 32-bit.
    if (antiCheatIn(processImage(process).parent_path())) return State::anticheat;
    if (hasLibrary(process)) return State::starting;
    const auto path = library.wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return State::failed;
    State result = State::failed;
    const auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (load && WriteProcessMemory(process, remote, path.c_str(), bytes, nullptr)) {
        if (const HANDLE thread = CreateRemoteThread(process, nullptr, 0, load, remote, 0, nullptr)) {
            DWORD loaded = 0;
            if (WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0 && GetExitCodeThread(thread, &loaded) && loaded) result = State::starting;
            CloseHandle(thread);
        }
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return result;
}
} // namespace

bool antiCheatIn(const std::filesystem::path& folder) {
    std::error_code error;
    if (folder.empty()) return false;
    for (std::filesystem::directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        const auto name = lower(it->path().filename().wstring());
        for (const auto* needle : anti_cheat_names) {
            // "vgc" is Vanguard: only as a file name's start, not inside "overgcc".
            if (std::wstring_view(needle) == L"vgc" ? name.rfind(needle, 0) == 0 : name.find(needle) != std::wstring::npos) return true;
        }
    }
    return false;
}

struct Controller::Impl {
    struct Entry {
        HANDLE mapping = nullptr;
        Shared* view = nullptr;
        State state = State::off;
        bool injecting = false;
        ULONGLONG started = 0;
        DWORD last_presents = 0;
    };
    std::mutex mutex;
    std::map<uint32_t, Entry> games;
    std::filesystem::path folder;

    ~Impl() {
        for (auto& [pid, entry] : games) release(entry);
    }
    static void release(Entry& entry) {
        if (entry.view) UnmapViewOfFile(entry.view);
        if (entry.mapping) CloseHandle(entry.mapping);
        entry.view = nullptr; entry.mapping = nullptr;
    }
};

Controller::Controller() : impl_(std::make_shared<Impl>()) {
    wchar_t path[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    impl_->folder = std::filesystem::path(path).parent_path();
}
Controller::~Controller() = default;
void Controller::library(std::filesystem::path path) {
    std::lock_guard lock(impl_->mutex);
    impl_->folder = std::move(path);
}

void Controller::apply(uint32_t pid, double fps, bool precise, uint32_t mode) {
    if (!pid) return;
    std::shared_ptr<Impl> impl = impl_;
    std::lock_guard lock(impl->mutex);
    auto found = impl->games.find(pid);
    if (found == impl->games.end()) {
        if (fps <= 0) return; // Nothing to do for a game that never had a limit.
        Impl::Entry entry;
        entry.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), mappingName(pid).c_str());
        if (!entry.mapping) return;
        entry.view = static_cast<Shared*>(MapViewOfFile(entry.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (!entry.view) { CloseHandle(entry.mapping); return; }
        new (entry.view) Shared();
        found = impl->games.emplace(pid, entry).first;
    }
    auto& entry = found->second;
    entry.view->precise = precise ? 1 : 0;
    entry.view->mode = mode;
    entry.view->heartbeat = GetTickCount();
    entry.view->limit_x100 = fps > 0 ? static_cast<uint32_t>(std::clamp(fps, 1.0, 1000.0) * 100 + 0.5) : 0;
    if (fps <= 0 || entry.injecting || entry.state != State::off) return;
    entry.injecting = true; entry.state = State::starting; entry.started = GetTickCount64();
    const auto library = impl->folder / library_name;
    try {
        std::thread([impl, pid, library] {
            const State result = inject(pid, library);
            std::lock_guard lock(impl->mutex);
            if (const auto game = impl->games.find(pid); game != impl->games.end()) {
                game->second.injecting = false;
                game->second.state = result;
                game->second.started = GetTickCount64();
            }
        }).detach();
    } catch (...) { entry.injecting = false; entry.state = State::failed; }
}

void Controller::heartbeat() {
    std::lock_guard lock(impl_->mutex);
    for (auto it = impl_->games.begin(); it != impl_->games.end();) {
        auto& entry = it->second;
        const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, it->first);
        const bool alive = process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
        if (process) CloseHandle(process);
        if (!alive && !entry.injecting) { Impl::release(entry); it = impl_->games.erase(it); continue; }
        entry.view->heartbeat = GetTickCount();
        ++it;
    }
}

uint64_t Controller::heldMicroseconds(uint32_t pid) const {
    std::lock_guard lock(impl_->mutex);
    const auto found = impl_->games.find(pid);
    return found == impl_->games.end() ? 0 : found->second.view->held_us;
}

State Controller::state(uint32_t pid) const {
    std::lock_guard lock(impl_->mutex);
    const auto found = impl_->games.find(pid);
    if (found == impl_->games.end()) return State::off;
    const auto& entry = found->second;
    if (entry.state != State::starting || entry.injecting) return entry.state;
    if (entry.view->hooked == hooked_failed) return State::failed;
    if (entry.view->hooked == hooked_ok && entry.view->presents > 0) return State::active;
    return GetTickCount64() - entry.started > 8000 ? State::no_dxgi : State::starting;
}

} // namespace frame_limiter
