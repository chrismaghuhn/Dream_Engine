#pragma once

#include "engine/gameplay/BlockRegistry.hpp"

namespace engine {

struct CreativeBlockPicker {
    BlockId selected = BLOCK_STONE;

    void set_selected(BlockId id) { selected = id; }
    [[nodiscard]] BlockId selected_id() const { return selected; }
};

} // namespace engine
