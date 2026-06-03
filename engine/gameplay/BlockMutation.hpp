#pragma once

#include "engine/world/BlockPos.hpp"
#include "engine/gameplay/BlockState.hpp"

#include <cstdint>

namespace engine {

struct BlockMutation {
    BlockPos pos{};
    BlockState old_state{};
    BlockState new_state{};
    uint64_t source_entity = 0;
    uint64_t tick = 0;
};

struct BlockMutationResult {
    bool applied = false;
};

} // namespace engine
