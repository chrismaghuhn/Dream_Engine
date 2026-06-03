#include "engine/physics/CollisionRemeshQueue.hpp"

#include <chrono>

namespace engine {

namespace {

[[nodiscard]] std::int64_t chunk_coord_key(const ChunkCoord coord) {
    return (static_cast<std::int64_t>(coord.x) << 42) ^
           (static_cast<std::int64_t>(coord.y) << 21) ^
           static_cast<std::int64_t>(coord.z);
}

} // namespace

std::int64_t CollisionRemeshQueue::chunk_key(const ChunkCoord coord) {
    return chunk_coord_key(coord);
}

void CollisionRemeshQueue::enqueue(const ChunkCoord coord) {
    const std::int64_t key = chunk_key(coord);
    if (pending_set_.insert(key).second) {
        pending_.push_back(coord);
    }
}

void CollisionRemeshQueue::process(const float budget_ms) {
    if (pending_.empty() || budget_ms <= 0.f) {
        return;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<float, std::milli>(budget_ms));

    while (!pending_.empty()) {
        const ChunkCoord coord = pending_.front();
        pending_.pop_front();
        pending_set_.erase(chunk_key(coord));

        // TODO(M11): build simplified greedy mesh and swap Jolt MeshShape for this chunk.
        (void)coord;

        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
    }
}

} // namespace engine
