#pragma once

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

struct BorderCell {
    BlockState block{};
    uint8_t    sky_light = 0;
    uint8_t    block_light = 0;
};

struct SectionBorderCache {
    std::array<BorderCell, SECTION_DIM * SECTION_DIM> face[6]{};
    bool dirty = true;
};

struct SectionOccupancy {
    static constexpr int kBitCount = SECTION_DIM * SECTION_DIM * SECTION_DIM;
    std::array<uint64_t, kBitCount / 64> bits{};

    [[nodiscard]] bool test(int x, int y, int z) const {
        const int idx = block_index(x, y, z);
        return (bits[static_cast<size_t>(idx / 64)] >> (idx % 64)) & 1u;
    }

    void set(int x, int y, int z, bool solid) {
        const int idx = block_index(x, y, z);
        const size_t word = static_cast<size_t>(idx / 64);
        const uint64_t mask = 1ull << (idx % 64);
        if (solid) {
            bits[word] |= mask;
        } else {
            bits[word] &= ~mask;
        }
    }
};

struct Section {
    static constexpr size_t kMaxPaletteEntries = SECTION_DIM * SECTION_DIM * SECTION_DIM;

    std::vector<BlockState> palette{};
    std::array<uint16_t, kMaxPaletteEntries> blocks{};
    std::array<uint8_t, kMaxPaletteEntries> sky_light{};
    std::array<uint8_t, kMaxPaletteEntries> block_light{};
    SectionOccupancy occupancy{};
    SectionBorderCache border{};

    Section() {
        palette.push_back(make_block_state(BLOCK_AIR, 0));
        blocks.fill(0);
    }

    [[nodiscard]] BlockState read_block(int x, int y, int z) const {
        const uint16_t idx = blocks[static_cast<size_t>(block_index(x, y, z))];
        return palette[idx];
    }

    [[nodiscard]] bool write_block(int x, int y, int z, BlockState state) {
        const uint16_t palette_idx = palette_index_for(state);
        if (palette_idx == std::numeric_limits<uint16_t>::max()) {
            return false;
        }
        blocks[static_cast<size_t>(block_index(x, y, z))] = palette_idx;
        return true;
    }

    [[nodiscard]] bool occupancy_bit(glm::ivec3 blk) const {
        return occupancy.test(blk.x, blk.y, blk.z);
    }

    void sync_occupancy_from_blocks() {
        for (int y = 0; y < SECTION_DIM; ++y) {
            for (int z = 0; z < SECTION_DIM; ++z) {
                for (int x = 0; x < SECTION_DIM; ++x) {
                    const bool solid = is_solid(block_id(read_block(x, y, z)));
                    occupancy.set(x, y, z, solid);
                }
            }
        }
    }

private:
    [[nodiscard]] uint16_t palette_index_for(BlockState state) {
        for (uint16_t i = 0; i < palette.size(); ++i) {
            if (palette[i].raw == state.raw) {
                return i;
            }
        }
        if (palette.size() >= kMaxPaletteEntries) {
            return std::numeric_limits<uint16_t>::max();
        }
        const uint16_t idx = static_cast<uint16_t>(palette.size());
        palette.push_back(state);
        return idx;
    }
};

} // namespace engine
