#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Profiler.h"
#include "core/ThreadSafeQueue.h"
#include "voxel/ChunkJobs.h"

namespace voxel {
class ChunkMesher;
class ChunkRegistry;
} // namespace voxel

namespace core {

class WorkerPool {
public:
    WorkerPool() = default;
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void Start(std::size_t threadCount,
               ThreadSafeQueue<voxel::GenerateJob>& generateQueue,
               ThreadSafeQueue<voxel::MeshJob>& meshQueue,
               ThreadSafeQueue<voxel::MeshReady>& readyQueue,
               voxel::ChunkRegistry& registry,
               const voxel::ChunkMesher& mesher,
               core::Profiler* profiler);
    void Stop();
    void NotifyWork();

    std::size_t ThreadCount() const;

private:
    void WorkerLoop();
    void ExecuteGenerate(const voxel::GenerateJob& job);
    void ExecuteMesh(const voxel::MeshJob& job);

    std::atomic<bool> stop_{false};
    std::vector<std::thread> threads_;

    ThreadSafeQueue<voxel::GenerateJob>* generateQueue_ = nullptr;
    ThreadSafeQueue<voxel::MeshJob>* meshQueue_ = nullptr;
    ThreadSafeQueue<voxel::MeshReady>* readyQueue_ = nullptr;
    voxel::ChunkRegistry* registry_ = nullptr;
    const voxel::ChunkMesher* mesher_ = nullptr;
    core::Profiler* profiler_ = nullptr;

    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
};

} // namespace core
