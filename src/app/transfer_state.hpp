#pragma once
// Ustawienia → System → Kopia i przenoszenie in Launcher (src/app/transfer.cpp): export, a backup's import,
// Playnite's import, and the one job running in the background with its progress.
#include "../library/playnite_import.hpp"
#include "../platform/profile_transfer.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace launcher_state {
struct TransferState {
    enum class Job { none, measuring, exporting, staging, reading_playnite };
    Job job = Job::none;
    uint64_t done = 0, total = 0;               // The job's bytes so far (exporting, staging).
    std::shared_ptr<std::atomic_bool> cancel;   // Anuluj: the job stops at its next file.
    uint32_t generation = 0;
    std::wstring status;                        // The last job's result line ("Kopia gotowa: …", an error).
    bool status_error = false;
    bool restart_ready = false;                 // An import is staged: Uruchom ponownie.

    // Export.
    std::set<profile_transfer::Part> export_parts{profile_transfer::Part::settings, profile_transfer::Part::library,
                                                  profile_transfer::Part::artwork};
    std::map<profile_transfer::Part, profile_transfer::PartSize> sizes;
    std::wstring password;                      // Typed once per session; sealed parts need it.

    // A picked backup.
    std::filesystem::path zip;
    std::optional<profile_transfer::Manifest> manifest;
    std::set<profile_transfer::Part> import_parts;
    profile_transfer::Mode mode = profile_transfer::Mode::merge;
    std::wstring paths;                         // "D:\ROMs => /mnt/roms; …"

    // Playnite.
    std::filesystem::path playnite_source;
    std::shared_ptr<const playnite::Library> playnite;
    playnite::Options playnite_options;
};
} // namespace launcher_state
