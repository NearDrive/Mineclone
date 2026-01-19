#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace core {

enum class Metric : std::size_t {
    Frame = 0,
    Update,
    Upload,
    Render,
    Generate,
    Mesh,
    Count
};

struct ProfilerSnapshot {
    double windowSeconds = 0.0;
    std::array<double, static_cast<std::size_t>(Metric::Count)> avgMs{};
    std::array<double, static_cast<std::size_t>(Metric::Count)> emaMs{};
    std::array<std::int64_t, static_cast<std::size_t>(Metric::Count)> counts{};
};

class Profiler {
public:
    Profiler();

    void AddSample(Metric metric, std::chrono::microseconds duration);
    ProfilerSnapshot CollectSnapshot(double emaAlpha = 0.2);

private:
    std::array<std::atomic<std::int64_t>, static_cast<std::size_t>(Metric::Count)> totalsUs_{};
    std::array<std::atomic<std::int64_t>, static_cast<std::size_t>(Metric::Count)> counts_{};
    std::array<double, static_cast<std::size_t>(Metric::Count)> emaMs_{};
    std::chrono::steady_clock::time_point lastWindow_;
};

class ScopedTimer {
public:
    ScopedTimer(Profiler* profiler, Metric metric);
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    Profiler* profiler_ = nullptr;
    Metric metric_ = Metric::Frame;
    std::chrono::steady_clock::time_point start_;
    bool active_ = false;
};

} // namespace core
