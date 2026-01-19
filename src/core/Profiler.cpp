#include "core/Profiler.h"

namespace core {

Profiler::Profiler() : lastWindow_(std::chrono::steady_clock::now()) {
    for (auto& value : totalsUs_) {
        value.store(0, std::memory_order_relaxed);
    }
    for (auto& value : counts_) {
        value.store(0, std::memory_order_relaxed);
    }
    emaMs_.fill(0.0);
}

void Profiler::AddSample(Metric metric, std::chrono::microseconds duration) {
    const std::size_t index = static_cast<std::size_t>(metric);
    totalsUs_[index].fetch_add(duration.count(), std::memory_order_relaxed);
    counts_[index].fetch_add(1, std::memory_order_relaxed);
}

ProfilerSnapshot Profiler::CollectSnapshot(double emaAlpha) {
    ProfilerSnapshot snapshot;
    const auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> window = now - lastWindow_;
    snapshot.windowSeconds = window.count();
    lastWindow_ = now;

    for (std::size_t i = 0; i < static_cast<std::size_t>(Metric::Count); ++i) {
        const std::int64_t totalUs = totalsUs_[i].exchange(0, std::memory_order_acq_rel);
        const std::int64_t count = counts_[i].exchange(0, std::memory_order_acq_rel);
        snapshot.counts[i] = count;

        double avgMs = 0.0;
        if (count > 0) {
            avgMs = static_cast<double>(totalUs) / static_cast<double>(count) / 1000.0;
        }
        snapshot.avgMs[i] = avgMs;

        double previous = emaMs_[i];
        if (count > 0) {
            emaMs_[i] = previous == 0.0 ? avgMs : (emaAlpha * avgMs + (1.0 - emaAlpha) * previous);
        }
        snapshot.emaMs[i] = emaMs_[i];
    }

    return snapshot;
}

ScopedTimer::ScopedTimer(Profiler* profiler, Metric metric)
    : profiler_(profiler),
      metric_(metric),
      start_(std::chrono::steady_clock::now()),
      active_(profiler != nullptr) {}

ScopedTimer::~ScopedTimer() {
    if (!active_) {
        return;
    }
    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
    profiler_->AddSample(metric_, duration);
}

} // namespace core
