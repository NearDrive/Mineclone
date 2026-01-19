#include "core/WorkerPool.h"

#include <chrono>
#include <iostream>
#include <shared_mutex>

#include "voxel/ChunkMesher.h"
#include "voxel/ChunkRegistry.h"

namespace core {

void WorkerPool::Start(std::size_t threadCount,
                       ThreadSafeQueue<voxel::GenerateJob>& generateQueue,
                       ThreadSafeQueue<voxel::MeshJob>& meshQueue,
                       ThreadSafeQueue<voxel::MeshReady>& readyQueue,
                       voxel::ChunkRegistry& registry,
                       const voxel::ChunkMesher& mesher) {
    Stop();

    stop_.store(false);
    generateQueue_ = &generateQueue;
    meshQueue_ = &meshQueue;
    readyQueue_ = &readyQueue;
    registry_ = &registry;
    mesher_ = &mesher;

    threads_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        threads_.emplace_back([this]() { WorkerLoop(); });
    }

    std::cout << "[Workers] Started " << threads_.size() << " worker thread(s).\n";
}

void WorkerPool::Stop() {
    if (threads_.empty()) {
        return;
    }

    stop_.store(true);
    wakeCv_.notify_all();

    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

void WorkerPool::NotifyWork() {
    wakeCv_.notify_one();
}

std::size_t WorkerPool::ThreadCount() const {
    return threads_.size();
}

void WorkerPool::WorkerLoop() {
    while (!stop_.load()) {
        voxel::GenerateJob generateJob;
        if (generateQueue_ && generateQueue_->try_pop(generateJob)) {
            ExecuteGenerate(generateJob);
            continue;
        }

        voxel::MeshJob meshJob;
        if (meshQueue_ && meshQueue_->try_pop(meshJob)) {
            ExecuteMesh(meshJob);
            continue;
        }

        std::unique_lock<std::mutex> lock(wakeMutex_);
        wakeCv_.wait_for(lock, std::chrono::milliseconds(2), [&]() {
            return stop_.load() ||
                   (generateQueue_ && !generateQueue_->empty()) ||
                   (meshQueue_ && !meshQueue_->empty());
        });
    }
}

void WorkerPool::ExecuteGenerate(const voxel::GenerateJob& job) {
    auto entry = job.entry.lock();
    if (!entry) {
        std::cout << "[Workers] Dropped generate job for expired chunk.\n";
        return;
    }
    if (!entry->wanted.load()) {
        std::cout << "[Workers] Dropped generate job for unloaded chunk.\n";
        return;
    }

    voxel::GenerationState expected = voxel::GenerationState::Queued;
    if (!entry->generationState.compare_exchange_strong(expected, voxel::GenerationState::Generating)) {
        return;
    }

    voxel::Chunk chunk;
    voxel::ChunkRegistry::GenerateChunkData(job.coord, chunk);

    {
        std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
        entry->chunk = std::make_unique<voxel::Chunk>(std::move(chunk));
    }

    entry->generationState.store(voxel::GenerationState::Ready, std::memory_order_release);

    if (!entry->wanted.load()) {
        std::cout << "[Workers] Generated chunk then found it unloaded.\n";
    }
}

void WorkerPool::ExecuteMesh(const voxel::MeshJob& job) {
    auto entry = job.entry.lock();
    if (!entry) {
        std::cout << "[Workers] Dropped mesh job for expired chunk.\n";
        return;
    }
    if (!entry->wanted.load()) {
        std::cout << "[Workers] Dropped mesh job for unloaded chunk.\n";
        return;
    }

    voxel::MeshingState expected = voxel::MeshingState::Queued;
    if (!entry->meshingState.compare_exchange_strong(expected, voxel::MeshingState::Meshing)) {
        return;
    }

    if (entry->generationState.load(std::memory_order_acquire) != voxel::GenerationState::Ready) {
        entry->meshingState.store(voxel::MeshingState::NotScheduled);
        std::cout << "[Workers] Mesh job skipped; chunk not generated yet.\n";
        return;
    }

    std::shared_lock<std::shared_mutex> chunkLock(entry->dataMutex);
    const voxel::Chunk* chunk = entry->chunk.get();
    if (!chunk) {
        entry->meshingState.store(voxel::MeshingState::NotScheduled);
        std::cout << "[Workers] Mesh job skipped; chunk missing.\n";
        return;
    }

    voxel::ChunkMeshCpu cpuMesh;
    mesher_->BuildMesh(job.coord, *chunk, *registry_, cpuMesh);

    auto meshPayload = std::make_shared<voxel::ChunkMeshCpu>(std::move(cpuMesh));
    readyQueue_->push(voxel::MeshReady{job.coord, job.entry, std::move(meshPayload)});
    entry->meshingState.store(voxel::MeshingState::Ready, std::memory_order_release);
    entry->gpuState.store(voxel::GpuState::UploadQueued, std::memory_order_release);
}

} // namespace core
