#pragma once

#include "engine/core/math.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_set>

namespace engine {

class CollisionRemeshQueue {
public:
    void enqueue(ChunkCoord coord);
    void process(float budget_ms);

    [[nodiscard]] bool has_pending() const { return !pending_.empty(); }
    [[nodiscard]] std::size_t pending_count() const { return pending_.size(); }

private:
    [[nodiscard]] static std::int64_t chunk_key(ChunkCoord coord);

    std::deque<ChunkCoord> pending_{};
    std::unordered_set<std::int64_t> pending_set_{};
};

} // namespace engine
