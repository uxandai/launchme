#pragma once
#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <atomic>
#include <string>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>

namespace atomic_file {
namespace detail {
#ifdef _WIN32
inline unsigned long processId() { return GetCurrentProcessId(); }
inline bool rename(const std::filesystem::path& from, const std::filesystem::path& to, bool replace) {
    return MoveFileExW(from.c_str(), to.c_str(), (replace ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH);
}
inline bool processRunning(unsigned long pid) {
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}
#else
inline unsigned long processId() { return static_cast<unsigned long>(getpid()); }
// rename(2) replaces atomically; without `replace`, link(2) fails on an existing target instead.
inline bool rename(const std::filesystem::path& from, const std::filesystem::path& to, bool replace) {
    if (replace) return ::rename(from.c_str(), to.c_str()) == 0;
    if (struct stat info{}; ::lstat(from.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
        // A folder cannot be linked: rename(2) only when nothing is at `to` (it would replace an empty folder).
        if (struct stat target{}; ::lstat(to.c_str(), &target) == 0 || errno != ENOENT) return false;
        return ::rename(from.c_str(), to.c_str()) == 0;
    }
    if (::link(from.c_str(), to.c_str()) != 0) return false;
    ::unlink(from.c_str());
    return true;
}
inline bool processRunning(unsigned long pid) { return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM; }
#endif
}
// Renames `from` over `to` (same file system), replacing it atomically.
inline bool replace(const std::filesystem::path& from, const std::filesystem::path& to) { return detail::rename(from, to, true); }
// Renames `from` to `to`, failing when `to` already exists.
inline bool moveNew(const std::filesystem::path& from, const std::filesystem::path& to) { return detail::rename(from, to, false); }

// An exclusive lock on `file` (created when missing) while this lives; locked() is false when
// another process or thread holds it. Windows: no sharing at all; elsewhere flock(2).
class ExclusiveLock {
public:
    explicit ExclusiveLock(const std::filesystem::path& file) {
#ifdef _WIN32
        handle_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
        handle_ = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (handle_ >= 0 && ::flock(handle_, LOCK_EX | LOCK_NB) != 0) { ::close(handle_); handle_ = -1; }
#endif
    }
    ~ExclusiveLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (handle_ >= 0) ::close(handle_);
#endif
    }
    ExclusiveLock(const ExclusiveLock&) = delete;
    ExclusiveLock& operator=(const ExclusiveLock&) = delete;
#ifdef _WIN32
    bool locked() const { return handle_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    bool locked() const { return handle_ >= 0; }
private:
    int handle_ = -1;
#endif
};

// Same-directory temporary files keep replacement atomic. Unique names allow
// independent cache workers; a failed write/rename leaves the previous file intact.
inline bool write(const std::filesystem::path& file, std::span<const uint8_t> bytes, bool replace = true) {
    static std::atomic_uint64_t serial{0};
    std::error_code error;
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), error);
    if (error) return false;
    const std::filesystem::path temporary = file.wstring() + L".tmp." + std::to_wstring(detail::processId()) + L'.' + std::to_wstring(serial.fetch_add(1, std::memory_order_relaxed));
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (output) output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.flush(); const bool complete = output.good(); output.close();
    if (complete && !output.fail() && detail::rename(temporary, file, replace)) return true;
    std::filesystem::remove(temporary, error);
    return false;
}
inline bool write(const std::filesystem::path& file, std::string_view text) {
    return write(file, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}
// For the user's own state (settings, library state): the new contents reach the disk before
// the rename, so a power cut leaves either the old file or the new one, never an empty file.
inline bool writeDurable(const std::filesystem::path& file, std::string_view text) {
    static std::atomic_uint64_t serial{0};
    std::error_code error;
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), error);
    if (error) return false;
    const auto temporary = file.wstring() + L".tmp." + std::to_wstring(detail::processId()) + L".d" + std::to_wstring(serial.fetch_add(1, std::memory_order_relaxed));
#ifdef _WIN32
    const HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    bool complete = true;
    for (size_t offset = 0; complete && offset < text.size();) {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<size_t>(text.size() - offset, 1u << 20));
        complete = WriteFile(handle, text.data() + offset, chunk, &written, nullptr) && written == chunk;
        offset += written;
    }
    complete = complete && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (complete && detail::rename(temporary, file, true)) return true;
#else
    const std::filesystem::path temporary_path(temporary);
    const int handle = ::open(temporary_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (handle < 0) return false;
    bool complete = true;
    for (size_t offset = 0; complete && offset < text.size();) {
        const ssize_t written = ::write(handle, text.data() + offset, std::min<size_t>(text.size() - offset, 1u << 20));
        if (written < 0 && errno == EINTR) continue;
        complete = written > 0;
        if (complete) offset += static_cast<size_t>(written);
    }
    complete = complete && ::fsync(handle) == 0;
    ::close(handle);
    if (complete && detail::rename(temporary_path, file, true)) {
        // The rename itself reaches the disk with its directory.
        const auto folder = file.parent_path().empty() ? std::filesystem::path(".") : file.parent_path();
        if (const int directory = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); directory >= 0) {
            ::fsync(directory);
            ::close(directory);
        }
        return true;
    }
#endif
    std::filesystem::remove(temporary, error);
    return false;
}
// Temporary files a crashed or killed process left next to `folder`'s files (*.tmp.<pid>.<n>).
// Only files of processes no longer running are removed.
inline void removeStaleTemporaries(const std::filesystem::path& folder) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().wstring();
        const auto at = name.find(L".tmp.");
        if (at == std::wstring::npos || !it->is_regular_file(error)) continue;
        const auto pid_text = name.substr(at + 5, name.find(L'.', at + 5) - (at + 5));
        unsigned long pid = 0;
        try { pid = std::stoul(pid_text); } catch (...) { continue; }
        if (pid == detail::processId() || detail::processRunning(pid)) continue;
        std::filesystem::remove(it->path(), error);
        error.clear();
    }
}
}
