#include "engine/procgen/TerrainGraph.hpp"

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/world/SectionIndexing.hpp"
#include "engine/world/TerrainLod.hpp"

#include <FastNoise/FastNoise.h>

#include <glm/glm.hpp>

#include <memory>

namespace engine {

namespace {

constexpr float kHeightFrequency = 0.004f;
constexpr float kBiomeFrequency = 0.0015f;
constexpr float kCaveFrequency = 0.035f;
constexpr int kStructureGrid = 16;
constexpr int kStructureMargin = 4;
constexpr float kStructureDensity = 0.08f;

uint64_t mix_seed(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;
    return value;
}

FastNoise::SmartNode<FastNoise::FractalFBm> make_fractal_height_node() {
    auto base = FastNoise::New<FastNoise::OpenSimplex2>();
    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    fractal->SetSource(base);
    fractal->SetOctaveCount(5);
    fractal->SetGain(0.5f);
    fractal->SetLacunarity(2.0f);
    return fractal;
}

} // namespace

struct TerrainGraph::NoiseNodes {
    FastNoise::SmartNode<FastNoise::FractalFBm> height;
    FastNoise::SmartNode<FastNoise::FractalFBm> temperature;
    FastNoise::SmartNode<FastNoise::FractalFBm> moisture;
    FastNoise::SmartNode<FastNoise::OpenSimplex2> cave;
};

TerrainGraph::TerrainGraph(uint64_t world_seed, int sea_level)
    : world_seed_(world_seed), sea_level_(sea_level), nodes_(std::make_unique<NoiseNodes>()) {
    nodes_->height = make_fractal_height_node();
    nodes_->temperature = make_fractal_height_node();
    nodes_->moisture = make_fractal_height_node();
    nodes_->cave = FastNoise::New<FastNoise::OpenSimplex2>();
}

TerrainGraph::~TerrainGraph() = default;

int TerrainGraph::noise_seed() const {
    return static_cast<int>(world_seed_ & 0x7fffffffu);
}

int TerrainGraph::surface_height_at(int wx, int wz) const {
    const float height =
        nodes_->height->GenSingle2D(static_cast<float>(wx) * kHeightFrequency,
            static_cast<float>(wz) * kHeightFrequency,
            noise_seed());
    return sea_level_ + static_cast<int>(height * 32.f);
}

Biome TerrainGraph::biome_at(int wx, int wz) const {
    const int surface = surface_height_at(wx, wz);
    if (surface < sea_level_) {
        return Biome::Ocean;
    }

    const int biome_seed = noise_seed() ^ 0x51ed270b;
    const float temperature = nodes_->temperature->GenSingle2D(
        static_cast<float>(wx) * kBiomeFrequency,
        static_cast<float>(wz) * kBiomeFrequency,
        biome_seed);
    const float moisture = nodes_->moisture->GenSingle2D(
        static_cast<float>(wx) * kBiomeFrequency,
        static_cast<float>(wz + 9973) * kBiomeFrequency,
        biome_seed ^ 0x6c078965);

    if (temperature < -0.25f && surface > sea_level_ + 36) {
        return Biome::Mountains;
    }
    if (moisture < -0.2f) {
        return Biome::Desert;
    }
    if (temperature > 0.15f && moisture > 0.05f) {
        return Biome::Hills;
    }
    return Biome::Plains;
}

bool TerrainGraph::is_cave_at(int wx, int wy, int wz, int surface) const {
    if (wy >= surface - 2 || wy <= 8) {
        return false;
    }

    const float density = nodes_->cave->GenSingle3D(static_cast<float>(wx) * kCaveFrequency,
        static_cast<float>(wy) * kCaveFrequency,
        static_cast<float>(wz) * kCaveFrequency,
        noise_seed() ^ 0x243f6a88);
    return density > 0.58f;
}

BlockState TerrainGraph::column_block(int wx, int wy, int wz, int surface, Biome biome) const {
    if (is_cave_at(wx, wy, wz, surface)) {
        return make_block_state(BLOCK_AIR, 0);
    }

    if (wy > surface) {
        if (wy <= sea_level_) {
            return make_block_state(BLOCK_WATER, 0);
        }
        return make_block_state(BLOCK_AIR, 0);
    }

    if (wy == surface) {
        if (surface <= sea_level_) {
            return make_block_state(BLOCK_DIRT, 0);
        }

        switch (biome) {
        case Biome::Desert:
            return make_block_state(BLOCK_DIRT, 0);
        case Biome::Mountains:
        case Biome::Hills:
            return surface > sea_level_ + 28 ? make_block_state(BLOCK_STONE, 0)
                                             : make_block_state(BLOCK_GRASS, 0);
        case Biome::Ocean:
        case Biome::Plains:
        default:
            return make_block_state(BLOCK_GRASS, 0);
        }
    }

    const int dirt_depth = biome == Biome::Desert ? 2 : 4;
    if (wy > surface - dirt_depth) {
        return make_block_state(BLOCK_DIRT, 0);
    }

    return make_block_state(BLOCK_STONE, 0);
}

BlockState TerrainGraph::block_at_world(int wx, int wy, int wz) const {
    const int surface = surface_height_at(wx, wz);
    const Biome biome = biome_at(wx, wz);
    return column_block(wx, wy, wz, surface, biome);
}

void TerrainGraph::stamp_structures(Chunk& chunk) const {
    const int base_x = chunk.coord.x * 32;
    const int base_y = chunk.coord.y * 32;
    const int base_z = chunk.coord.z * 32;

    const int grid_min_x = (base_x - 31) / kStructureGrid;
    const int grid_max_x = (base_x + 31) / kStructureGrid;
    const int grid_min_z = (base_z - 31) / kStructureGrid;
    const int grid_max_z = (base_z + 31) / kStructureGrid;

    auto write_world_block = [&](int wx, int wy, int wz, BlockState state) {
        if (wx < base_x || wx >= base_x + 32 || wy < base_y || wy >= base_y + 32
            || wz < base_z || wz >= base_z + 32) {
            return;
        }

        const int lx = wx - base_x;
        const int ly = wy - base_y;
        const int lz = wz - base_z;
        const glm::ivec3 sec = { lx >> 4, ly >> 4, lz >> 4 };
        const glm::ivec3 blk = { lx & 15, ly & 15, lz & 15 };
        chunk.section_at(sec).write_block(blk.x, blk.y, blk.z, state);
    };

    for (int gx = grid_min_x; gx <= grid_max_x; ++gx) {
        for (int gz = grid_min_z; gz <= grid_max_z; ++gz) {
            const int anchor_x = gx * kStructureGrid + 8;
            const int anchor_z = gz * kStructureGrid + 8;
            if (anchor_x < base_x || anchor_x >= base_x + 32 || anchor_z < base_z
                || anchor_z >= base_z + 32) {
                continue;
            }

            const uint64_t hash = mix_seed(world_seed_ ^ static_cast<uint64_t>(gx) * 0x9e3779b97f4a7c15ull
                ^ static_cast<uint64_t>(gz) * 0x94d049bb133111ebull);
            const float roll = static_cast<float>(hash % 10000) / 10000.f;
            if (roll >= kStructureDensity) {
                continue;
            }

            const int surface = surface_height_at(anchor_x, anchor_z);
            if (surface < sea_level_ + 1) {
                continue;
            }

            const int local_x = anchor_x - base_x;
            const int local_z = anchor_z - base_z;
            const bool fits_medium = local_x >= kStructureMargin && local_x < 32 - kStructureMargin
                && local_z >= kStructureMargin && local_z < 32 - kStructureMargin;

            const int kind = static_cast<int>((hash >> 16) % 100);
            if (kind < 55 && fits_medium) {
                const int trunk_height = 4 + static_cast<int>((hash >> 24) % 3);
                for (int dy = 1; dy <= trunk_height; ++dy) {
                    write_world_block(anchor_x, surface + dy, anchor_z, make_block_state(BLOCK_STONE, 0));
                }
                for (int dx = -2; dx <= 2; ++dx) {
                    for (int dz = -2; dz <= 2; ++dz) {
                        for (int dy = trunk_height; dy <= trunk_height + 2; ++dy) {
                            if (std::abs(dx) + std::abs(dz) > 3) {
                                continue;
                            }
                            write_world_block(anchor_x + dx,
                                surface + dy,
                                anchor_z + dz,
                                make_block_state(BLOCK_GRASS, 0));
                        }
                    }
                }
            } else if (kind < 80 && fits_medium) {
                const int radius = 2 + static_cast<int>((hash >> 20) % 2);
                for (int dx = -radius; dx <= radius; ++dx) {
                    for (int dz = -radius; dz <= radius; ++dz) {
                        if (dx * dx + dz * dz > radius * radius) {
                            continue;
                        }
                        write_world_block(anchor_x + dx,
                            surface + 1,
                            anchor_z + dz,
                            make_block_state(BLOCK_STONE, 0));
                    }
                }
            } else {
                write_world_block(anchor_x, surface + 1, anchor_z, make_block_state(BLOCK_GRASS, 0));
            }
        }
    }
}

void TerrainGraph::fill_chunk(Chunk& chunk) const {
    const int base_x = chunk.coord.x * 32;
    const int base_y = chunk.coord.y * 32;
    const int base_z = chunk.coord.z * 32;

    for (int ly = 0; ly < 32; ++ly) {
        for (int lz = 0; lz < 32; ++lz) {
            for (int lx = 0; lx < 32; ++lx) {
                const int wx = base_x + lx;
                const int wy = base_y + ly;
                const int wz = base_z + lz;

                const int surface = surface_height_at(wx, wz);
                const Biome biome = biome_at(wx, wz);
                const BlockState state = column_block(wx, wy, wz, surface, biome);
                const glm::ivec3 sec = { lx >> 4, ly >> 4, lz >> 4 };
                const glm::ivec3 blk = { lx & 15, ly & 15, lz & 15 };
                chunk.section_at(sec).write_block(blk.x, blk.y, blk.z, state);
            }
        }
    }

    stamp_structures(chunk);

    for (Section& section : chunk.sections) {
        section.sync_occupancy_from_blocks();
    }
    recompute_chunk_render_meta(chunk);

    chunk.flags = CHUNK_GENERATED;
    chunk.flags &= static_cast<uint8_t>(~CHUNK_MODIFIED_BY_PLAYER);
}

} // namespace engine
