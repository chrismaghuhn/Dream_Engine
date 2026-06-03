#pragma once
#include <cstdint>

namespace engine {

using BlockId = uint16_t;

struct BlockState {
    uint16_t raw = 0;
};

constexpr uint16_t BLOCK_ID_MASK   = 0x0FFF;
constexpr uint16_t BLOCK_PROP_MASK = 0xF000;

inline BlockId block_id(BlockState s) { return BlockId(s.raw & BLOCK_ID_MASK); }
inline uint8_t block_props(BlockState s) { return uint8_t((s.raw & BLOCK_PROP_MASK) >> 12); }
inline BlockState make_block_state(BlockId id, uint8_t props) {
    return { uint16_t((uint16_t(id) & BLOCK_ID_MASK) | ((props & 0xF) << 12)) };
}

} // namespace engine
