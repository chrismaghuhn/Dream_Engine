#pragma once

#include "engine/core/math.hpp"
#include "engine/world/Section.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <array>
#include <cstdint>

namespace engine {

enum ChunkBlobFlags : uint8_t {
    CHUNK_GENERATED          = 1 << 0,
    CHUNK_MODIFIED_BY_PLAYER = 1 << 1,
};

struct Chunk {
    ChunkCoord coord{};
    std::array<Section, 8> sections{};
    uint8_t flags = 0;

    [[nodiscard]] Section& section_at(int sx, int sy, int sz) {
        return sections[static_cast<size_t>(section_index(sx, sy, sz))];
    }

    [[nodiscard]] const Section& section_at(int sx, int sy, int sz) const {
        return sections[static_cast<size_t>(section_index(sx, sy, sz))];
    }

    [[nodiscard]] Section& section_at(glm::ivec3 sec) {
        return section_at(sec.x, sec.y, sec.z);
    }

    [[nodiscard]] const Section& section_at(glm::ivec3 sec) const {
        return section_at(sec.x, sec.y, sec.z);
    }
};

} // namespace engine
