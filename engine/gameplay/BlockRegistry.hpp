#pragma once

#include "engine/gameplay/BlockState.hpp"

namespace engine {

constexpr BlockId BLOCK_AIR   = 0;
constexpr BlockId BLOCK_STONE = 1;
constexpr BlockId BLOCK_DIRT  = 2;
constexpr BlockId BLOCK_GRASS = 3;
constexpr BlockId BLOCK_WATER = 4;

inline bool is_solid(BlockId id) {
    return id != BLOCK_AIR && id != BLOCK_WATER;
}

} // namespace engine
