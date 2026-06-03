#pragma once

#include "engine/gameplay/BlockState.hpp"
#include "engine/world/BlockPos.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace engine {

struct SectionLightTarget {
    ChunkCoord  chunk{};
    glm::ivec3  section{};
};

/// Pending cross-section block-light work (deterministic drain order).
class BlockLightUpdateQueue {
public:
    void enqueue(ChunkCoord chunk, glm::ivec3 section);
    void clear();

    [[nodiscard]] bool empty() const { return pending_.empty(); }

    /// Process one section; returns false when queue is empty.
    bool drain_one(ChunkStore& store);

    /// Process until the queue is empty.
    void drain_all(ChunkStore& store);

    [[nodiscard]] const std::vector<SectionLightTarget>& pending() const { return pending_; }

private:
    std::vector<SectionLightTarget> pending_{};
};

[[nodiscard]] Section* neighbor_section(
    ChunkStore& store,
    ChunkCoord chunk,
    glm::ivec3 section_coord,
    Face face);

void refresh_section_border_cache(ChunkStore& store, ChunkCoord chunk, glm::ivec3 section_coord);

void flood_section_block_light(ChunkStore& store, BlockLightUpdateQueue& queue, ChunkCoord chunk,
                               glm::ivec3 section_coord);

/// Seed flood for a block change; enqueues the touched section and border neighbors.
[[nodiscard]] std::vector<ChunkCoord> on_block_light_block_changed(
    ChunkStore& store,
    BlockLightUpdateQueue& queue,
    BlockPos pos,
    BlockState old_state,
    BlockState new_state);

} // namespace engine
