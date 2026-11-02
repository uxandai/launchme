#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <numeric>
#include <vector>

namespace game_fps {
enum class Status { disabled, waiting, collecting, ready, denied, failed };
struct Reading {
    Status status = Status::disabled;
    double fps = 0, low = 0;
    std::vector<float> graph; // Frame times in ms, oldest first, evenly spread over the last six seconds.
    bool operator==(const Reading&) const = default;
};

// Present timestamps (seconds), kept per swapchain: summing two swapchains would
// double the FPS. The busiest recently active chain supplies both measurements.
// 1% low = reciprocal of the mean of the slowest ceil(N / 100) frame intervals
// in the last 60 seconds, not the reciprocal of the 99th percentile.
class History {
public:
    void clear() { chains_.clear(); }
    void present(uint64_t chain, double seconds) {
        if (!std::isfinite(seconds) || seconds <= 0) return;
        if (!chains_.contains(chain) && chains_.size() >= 32) {
            const auto oldest = std::min_element(chains_.begin(), chains_.end(), [](const auto& a, const auto& b) {
                return a.second.back() < b.second.back();
            });
            chains_.erase(oldest);
        }
        auto& frames = chains_[chain];
        if (!frames.empty() && seconds <= frames.back()) return;
        frames.push_back(seconds);
        while (frames.size() > 32768 || (frames.size() > 1 && frames.front() < seconds - 60)) frames.pop_front();
    }
    // The last six seconds as 96 equal slices: each holds the mean frame time of the frames shown in it, a slice
    // without a frame repeats the one before.
    static std::vector<float> graphOf(const std::deque<double>& frames) {
        constexpr int points = 96;
        constexpr double span = 6.0;
        std::vector<float> graph(points, 0.0f);
        if (frames.size() < 2) return graph;
        const double start = frames.back() - span;
        std::vector<double> sum(points, 0.0);
        std::vector<int> count(points, 0);
        auto it = std::lower_bound(frames.begin(), frames.end(), start);
        if (it == frames.begin()) ++it;
        for (; it != frames.end(); ++it) {
            const int slice = std::clamp(static_cast<int>((*it - start) / span * points), 0, points - 1);
            sum[slice] += *it - *(it - 1);
            ++count[slice];
        }
        float last = 0;
        for (int i = 0; i < points; ++i) {
            if (count[i]) last = static_cast<float>(sum[i] / count[i] * 1000.0);
            graph[i] = last;
        }
        return graph;
    }
    Reading read(double now) const {
        const std::deque<double>* best = nullptr;
        size_t best_count = 0;
        for (const auto& [chain, frames] : chains_) {
            (void)chain;
            if (frames.size() < 2 || now - frames.back() > 2.5 || frames.back() > now) continue;
            const auto first = std::lower_bound(frames.begin(), frames.end(), frames.back() - 1);
            const auto count = static_cast<size_t>(frames.end() - first);
            if (count > best_count) { best_count = count; best = &frames; }
        }
        if (!best || best_count < 2) return {Status::collecting};
        const auto& frames = *best;
        const double span = frames.back() - frames[frames.size() - best_count];
        if (span < 0.2) return {Status::collecting};
        Reading result{Status::ready, (best_count - 1) / span};
        result.graph = graphOf(frames);
        std::vector<double> intervals;
        for (size_t i = 1; i < frames.size(); ++i)
            if (frames[i - 1] >= now - 60) intervals.push_back(frames[i] - frames[i - 1]);
        if (intervals.size() >= 100) {
            const size_t count = (intervals.size() + 99) / 100;
            std::partial_sort(intervals.begin(), intervals.begin() + count, intervals.end(), std::greater<double>{});
            result.low = count / std::accumulate(intervals.begin(), intervals.begin() + count, 0.0);
        }
        return result;
    }
private:
    std::map<uint64_t, std::deque<double>> chains_;
};
}
