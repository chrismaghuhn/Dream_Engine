#include "engine/procgen/HeightmapWorldgen.hpp"

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <glm/glm.hpp>

namespace engine {

namespace {

uint64_t mix_seed(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;
    return value;
}

} // namespace

HeightmapWorldgen::HeightmapWorldgen(uint64_t world_seed, int sea_level)
    : world_seed_(world_seed), sea_level_(sea_level) {}

int HeightmapWorldgen::surface_height_at(int wx, int wz) const {
    uint64_t h = mix_seed(world_seed_ ^ static_cast<uint64_t>(wx) * 0x9e3779b97f4a7c15ull);
    h = mix_seed(h ^ static_cast<uint64_t>(wz) * 0x94d049bb133111ebull);

    constexpr int amplitude = 24;
    const int variation = static_cast<int>(h % static_cast<uint64_t>(amplitude * 2 + 1));
    return sea_level_ + variation - amplitude;
}

BlockState HeightmapWorldgen::block_at_world(int wx, int wy, int wz) const {
    const int surface = surface_height_at(wx, wz);

    if (wy > surface) {
        if (wy <= sea_level_) {
            return make_block_state(BLOCK_WATER, 0);
        }
        return make_block_state(BLOCK_AIR, 0);
    }

    if (wy == surface) {
        if (surface >= sea_level_) {
            return make_block_state(BLOCK_GRASS, 0);
        }
        return make_block_state(BLOCK_DIRT, 0);
    }

    if (wy > surface - 4) {
        return make_block_state(BLOCK_DIRT, 0);
    }

    return make_block_state(BLOCK_STONE, 0);
}

void HeightmapWorldgen::fill_chunk(Chunk& chunk) const {
    const int base_x = chunk.coord.x * 32;
    const int base_y = chunk.coord.y * 32;
    const int base_z = chunk.coord.z * 32;

    for (int ly = 0; ly < 32; ++ly) {
        for (int lz = 0; lz < 32; ++lz) {
            for (int lx = 0; lx < 32; ++lx) {
                const int wx = base_x + lx;
                const int wy = base_y + ly;
                const int wz = base_z + lz;

                const BlockState state = block_at_world(wx, wy, wz);
                const glm::ivec3 sec = { lx >> 4, ly >> 4, lz >> 4 };
                const glm::ivec3 blk = { lx & 15, ly & 15, lz & 15 };
                (void)chunk.section_at(sec).write_block(blk.x, blk.y, blk.z, state);
            }
        }
    }

    for (Section& section : chunk.sections) {
        section.sync_occupancy_from_blocks();
    }

    chunk.flags = CHUNK_GENERATED;
    chunk.flags &= static_cast<uint8_t>(~CHUNK_MODIFIED_BY_PLAYER);
}

} // namespace engine
