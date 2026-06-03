#pragma once

#include "engine/gameplay/BlockState.hpp"

namespace engine {

constexpr BlockId BLOCK_AIR   = 0;
constexpr BlockId BLOCK_STONE = 1;
constexpr BlockId BLOCK_DIRT  = 2;

inline bool is_solid(BlockId id) {
    return id != BLOCK_AIR;
}

} // namespace engine
