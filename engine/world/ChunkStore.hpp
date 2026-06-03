#pragma once

#include "engine/world/BlockPos.hpp"
#include "engine/world/Chunk.hpp"
#include "engine/world/WorldEvents.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine {

enum class OccupancyPolicy : uint8_t {
    SolidIfChunkMissing = 0,
    AirIfChunkMissing   = 1,
};

struct ChunkSlot {
    Chunk chunk{};
    uint32_t generation = 1;
    bool occupied = false;
    bool pending_unload = false;
};

struct ChunkCoordHash {
    size_t operator()(ChunkCoord c) const noexcept {
        size_t h = std::hash<int>()(c.x);
        h ^= std::hash<int>()(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

class ChunkStore {
public:
    [[nodiscard]] const Chunk* try_get(ChunkCoord coord) const;
    [[nodiscard]] Chunk* try_get(ChunkCoord coord);

    [[nodiscard]] Chunk* allocate(ChunkCoord coord);
    void free(ChunkCoord coord);

    [[nodiscard]] ChunkSlotRef slot_ref_for(ChunkCoord coord) const;
    [[nodiscard]] bool validate_slot_ref(ChunkSlotRef ref) const;
    [[nodiscard]] Chunk* try_get_via_ref(ChunkSlotRef ref);
    [[nodiscard]] const Chunk* try_get_via_ref(ChunkSlotRef ref) const;

    [[nodiscard]] bool is_pending_unload(ChunkCoord coord) const;
    void set_pending_unload(ChunkCoord coord, bool value);

    [[nodiscard]] uint64_t entity_for(ChunkCoord coord) const;
    void set_entity_for(ChunkCoord coord, uint64_t entity_id);
    void clear_entity_for(ChunkCoord coord);

    [[nodiscard]] BlockState read_block(BlockPos pos) const;
    [[nodiscard]] bool write_block(BlockPos pos, BlockState state);

private:
    std::vector<ChunkSlot> slots_{};
    std::vector<uint32_t> free_list_{};
    std::unordered_map<ChunkCoord, uint32_t, ChunkCoordHash> coord_to_slot_{};
    std::unordered_map<ChunkCoord, uint64_t, ChunkCoordHash> coord_to_entity_{};
};

bool occupancy_at(int wx, int wy, int wz, ChunkStore& store,
                  OccupancyPolicy policy = OccupancyPolicy::SolidIfChunkMissing);

} // namespace engine
