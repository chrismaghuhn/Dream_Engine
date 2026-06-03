#include "engine/audio/OcclusionGrid.hpp"

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/world/BlockPos.hpp"

#include <algorithm>
#include <cmath>

namespace engine {

namespace {

constexpr int kChunkSize = 32;

} // namespace

void OcclusionGrid::init(ChunkStore* store, const int radius_chunks) {
    store_ = store;
    radius_blocks_ = (std::max)(radius_chunks, 1) * kChunkSize;
    solid_cells_.clear();
    while (!pending_dirty_.empty()) {
        pending_dirty_.pop();
    }
    pending_set_.clear();
}

void OcclusionGrid::set_listener_origin(const glm::ivec3 world_blocks) {
    origin_ = world_blocks;
}

void OcclusionGrid::queue_chunk_dirty(const ChunkCoord coord) {
    const std::int64_t key =
        (static_cast<std::int64_t>(coord.x) << 42) ^
        (static_cast<std::int64_t>(coord.y) << 21) ^
        static_cast<std::int64_t>(coord.z);
    if (pending_set_.insert(key).second) {
        pending_dirty_.push(coord);
    }
}

void OcclusionGrid::process_pending(const std::uint32_t max_updates_per_tick) {
    std::uint32_t processed = 0;
    while (!pending_dirty_.empty() && processed < max_updates_per_tick) {
        const ChunkCoord coord = pending_dirty_.front();
        pending_dirty_.pop();
        const std::int64_t key =
            (static_cast<std::int64_t>(coord.x) << 42) ^
            (static_cast<std::int64_t>(coord.y) << 21) ^
            static_cast<std::int64_t>(coord.z);
        pending_set_.erase(key);
        refresh_chunk(coord);
        ++processed;
    }
}

std::int64_t OcclusionGrid::cell_key(const int wx, const int wy, const int wz) {
    return (static_cast<std::int64_t>(wx) << 42) ^
           (static_cast<std::int64_t>(wy) << 21) ^
           static_cast<std::int64_t>(wz);
}

bool OcclusionGrid::in_radius(const int wx, const int wy, const int wz) const {
    const int dx = wx - origin_.x;
    const int dy = wy - origin_.y;
    const int dz = wz - origin_.z;
    return std::abs(dx) <= radius_blocks_ && std::abs(dy) <= radius_blocks_ &&
           std::abs(dz) <= radius_blocks_;
}

bool OcclusionGrid::is_solid_at(const int wx, const int wy, const int wz) const {
    if (!in_radius(wx, wy, wz)) {
        return false;
    }
    const auto it = solid_cells_.find(cell_key(wx, wy, wz));
    if (it == solid_cells_.end()) {
        return false;
    }
    return it->second;
}

void OcclusionGrid::refresh_chunk(const ChunkCoord coord) {
    if (store_ == nullptr) {
        return;
    }

    const Chunk* chunk = store_->try_get(coord);
    if (chunk == nullptr) {
        return;
    }

    const glm::ivec3 base = glm::ivec3(coord) * kChunkSize;
    for (int lz = 0; lz < kChunkSize; ++lz) {
        for (int ly = 0; ly < kChunkSize; ++ly) {
            for (int lx = 0; lx < kChunkSize; ++lx) {
                const int wx = base.x + lx;
                const int wy = base.y + ly;
                const int wz = base.z + lz;
                if (!in_radius(wx, wy, wz)) {
                    continue;
                }

                const BlockPos pos{coord, {lx, ly, lz}};
                const BlockState state = store_->read_block(pos);
                const bool solid = is_solid(block_id(state));
                const std::int64_t key = cell_key(wx, wy, wz);
                if (solid) {
                    solid_cells_[key] = true;
                } else {
                    solid_cells_.erase(key);
                }
            }
        }
    }
}

float OcclusionGrid::occlusion_factor(const glm::vec3 listener, const glm::vec3 source) const {
    const glm::vec3 delta = source - listener;
    const float distance = glm::length(delta);
    if (distance < 0.05f) {
        return 0.f;
    }

    const glm::vec3 dir = delta / distance;
    const int steps = static_cast<int>(std::ceil(distance));
    int solid_hits = 0;

    for (int step = 1; step < steps; ++step) {
        const glm::vec3 sample = listener + dir * static_cast<float>(step);
        const int wx = static_cast<int>(std::floor(sample.x));
        const int wy = static_cast<int>(std::floor(sample.y));
        const int wz = static_cast<int>(std::floor(sample.z));
        if (is_solid_at(wx, wy, wz)) {
            ++solid_hits;
        }
    }

    if (solid_hits == 0) {
        return 0.f;
    }

    return (std::min)(1.f, static_cast<float>(solid_hits) / static_cast<float>(steps));
}

} // namespace engine
