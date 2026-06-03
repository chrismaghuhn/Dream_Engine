#pragma once

#include "engine/gameplay/BlockState.hpp"

#include <array>
#include <cstdint>

namespace engine {

constexpr BlockId BLOCK_AIR   = 0;
constexpr BlockId BLOCK_STONE = 1;
constexpr BlockId BLOCK_DIRT  = 2;
constexpr BlockId BLOCK_GRASS = 3;
constexpr BlockId BLOCK_WATER = 4;
constexpr BlockId BLOCK_TORCH = 5;

constexpr uint8_t MAX_BLOCK_LIGHT = 15;

struct BlockDef {
    uint8_t light_emission = 0;
    bool    blocks_light   = false;
};

inline constexpr std::array<BlockDef, 6> BLOCK_REGISTRY = {{
    {0, false},  // air
    {0, true},   // stone
    {0, true},   // dirt
    {0, true},   // grass
    {0, false},  // water
    {14, false}, // torch
}};

inline bool is_solid(BlockId id) {
    return id != BLOCK_AIR && id != BLOCK_WATER && id != BLOCK_TORCH;
}

inline bool is_water(BlockId id) {
    return id == BLOCK_WATER;
}

inline uint8_t light_emission(BlockId id) {
    if (id >= BLOCK_REGISTRY.size()) {
        return 0;
    }
    return BLOCK_REGISTRY[id].light_emission;
}

inline bool blocks_light(BlockId id) {
    if (id >= BLOCK_REGISTRY.size()) {
        return is_solid(id);
    }
    return BLOCK_REGISTRY[id].blocks_light;
}

inline bool block_change_affects_light(BlockState old_state, BlockState new_state) {
    const BlockId old_id = block_id(old_state);
    const BlockId new_id = block_id(new_state);
    return light_emission(old_id) != light_emission(new_id)
        || blocks_light(old_id) != blocks_light(new_id);
}

} // namespace engine
