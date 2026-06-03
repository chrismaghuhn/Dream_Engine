#pragma once

#include "engine/gameplay/BlockState.hpp"
#include "engine/world/Chunk.hpp"

#include <cstdint>
#include <memory>

namespace engine {

enum class Biome : std::uint8_t {
    Ocean,
    Plains,
    Hills,
    Desert,
    Mountains,
};

class TerrainGraph {
public:
    TerrainGraph(uint64_t world_seed, int sea_level);
    ~TerrainGraph();

    TerrainGraph(const TerrainGraph&) = delete;
    TerrainGraph& operator=(const TerrainGraph&) = delete;

    [[nodiscard]] BlockState block_at_world(int wx, int wy, int wz) const;
    void fill_chunk(Chunk& chunk) const;

    [[nodiscard]] int surface_height_at(int wx, int wz) const;
    [[nodiscard]] Biome biome_at(int wx, int wz) const;

private:
    [[nodiscard]] int noise_seed() const;
    [[nodiscard]] bool is_cave_at(int wx, int wy, int wz, int surface) const;
    [[nodiscard]] BlockState column_block(int wx, int wy, int wz, int surface, Biome biome) const;
    void stamp_structures(Chunk& chunk) const;

    uint64_t world_seed_;
    int sea_level_;

    struct NoiseNodes;
    std::unique_ptr<NoiseNodes> nodes_;
};

} // namespace engine
