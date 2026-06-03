#pragma once

#include "engine/core/math.hpp"
#include "engine/world/ChunkStore.hpp"

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace engine {

class OcclusionGrid {
public:
    void init(ChunkStore* store, int radius_chunks);
    void set_listener_origin(glm::ivec3 world_blocks);

    void queue_chunk_dirty(ChunkCoord coord);
    void process_pending(std::uint32_t max_updates_per_tick = 4);

    [[nodiscard]] float occlusion_factor(glm::vec3 listener, glm::vec3 source) const;

private:
    [[nodiscard]] static std::int64_t cell_key(int wx, int wy, int wz);
    void refresh_chunk(ChunkCoord coord);
    [[nodiscard]] bool is_solid_at(int wx, int wy, int wz) const;
    [[nodiscard]] bool in_radius(int wx, int wy, int wz) const;

    ChunkStore* store_ = nullptr;
    int radius_blocks_ = 32 * 32;
    glm::ivec3 origin_{0};
    std::unordered_map<std::int64_t, bool> solid_cells_{};
    std::queue<ChunkCoord> pending_dirty_{};
    std::unordered_set<std::int64_t> pending_set_{};
};

} // namespace engine
