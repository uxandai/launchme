#pragma once
#include "manager.hpp"
#include "downloads.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace plugins {
struct Job {
    enum class Kind { initialize, install, enable, setting, remove, search, command, widget, details, playtime, library, downloads, achievements, launch } kind = Kind::initialize;
    std::string plugin, key;
    json::Value value;
    std::filesystem::path file;
    Game game;
    uint32_t epoch = 0, generation = 0;
    std::string locale = "en";
    uint64_t account = 0;
    std::optional<uint64_t> session_started, playtime_seconds;
    bool active_game = false;
    bool refresh = false;    // command: F5 or a page's periodic refresh (params.refresh).
    bool background = false; // command: the periodic refresh, which does not block other commands;
                             // achievements: the desktop pane's selection, replaced by the next one.
    bool management() const { return kind <= Kind::remove; }
};
struct Queue {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
};
struct Result {
    Job job;
    Snapshot snapshot;
    json::Value response;
    std::string error;
};
}
