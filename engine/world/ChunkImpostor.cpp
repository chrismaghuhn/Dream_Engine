#include "engine/world/ChunkImpostor.hpp"

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <array>
#include <limits>

namespace engine {

namespace {

[[nodiscard]] glm::vec3 color_for_block_id(BlockId id) {
    switch (id) {
    case BLOCK_STONE:
        return glm::vec3(0.45f, 0.45f, 0.48f);
    case BLOCK_DIRT:
        return glm::vec3(0.45f, 0.35f, 0.25f);
    case BLOCK_GRASS:
        return glm::vec3(0.35f, 0.55f, 0.28f);
    default:
        return glm::vec3(0.45f);
    }
}

} // namespace

ChunkImpostorSummary compute_chunk_impostor(const Chunk& chunk) {
    ChunkImpostorSummary summary{};
    bool have_opaque = false;
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    std::array<std::uint32_t, 256> upper_half_counts{};

    for (int section_index = 0; section_index < 8; ++section_index) {
        const Section& section = chunk.sections[static_cast<std::size_t>(section_index)];
        const glm::ivec3 section_coord = section_coord_from_index(section_index);
        const int base_y = section_coord.y * SECTION_DIM;

        for (int y = 0; y < SECTION_DIM; ++y) {
            for (int z = 0; z < SECTION_DIM; ++z) {
                for (int x = 0; x < SECTION_DIM; ++x) {
                    const BlockId id = block_id(section.read_block(x, y, z));
                    if (!is_solid(id) || is_water(id)) {
                        continue;
                    }

                    const float chunk_y = static_cast<float>(base_y + y);
                    have_opaque = true;
                    min_y = std::min(min_y, chunk_y);
                    max_y = std::max(max_y, chunk_y);

                    if (chunk_y >= 16.f) {
                        ++upper_half_counts[static_cast<std::size_t>(id)];
                    }
                }
            }
        }
    }

    if (!have_opaque) {
        return summary;
    }

    summary.valid = true;
    summary.min_y = min_y;
    summary.max_y = max_y;

    BlockId dominant = BLOCK_STONE;
    std::uint32_t best_count = 0;
    for (std::size_t id = 0; id < upper_half_counts.size(); ++id) {
        const std::uint32_t count = upper_half_counts[id];
        if (count > best_count) {
            best_count = count;
            dominant = static_cast<BlockId>(id);
        }
    }

    summary.color = color_for_block_id(dominant);
    return summary;
}

} // namespace engine