#include "engine/world/TerrainLod.hpp"

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/world/BlockLight.hpp"

namespace engine {

void recompute_chunk_render_meta(Chunk& chunk) {
    ChunkRenderMeta meta{};
    for (const Section& section : chunk.sections) {
        for (int y = 0; y < SECTION_DIM; ++y) {
            for (int z = 0; z < SECTION_DIM; ++z) {
                for (int x = 0; x < SECTION_DIM; ++x) {
                    if (is_water(block_id(section.read_block(x, y, z)))) {
                        meta.has_water = true;
                        chunk.render_meta = meta;
                        return;
                    }
                }
            }
        }
    }
    chunk.render_meta = meta;
}

bool chunk_force_lod0_water_border(const ChunkStore& store, ChunkCoord coord) {
    const Chunk* chunk = store.try_get(coord);
    if (chunk == nullptr) {
        return false;
    }
    if (chunk->render_meta.has_water) {
        return true;
    }

    static constexpr ChunkCoord kXzNeighbors[] = {
        {1, 0, 0},
        {-1, 0, 0},
        {0, 0, 1},
        {0, 0, -1},
    };

    for (const ChunkCoord offset : kXzNeighbors) {
        const ChunkCoord neighbor_coord{coord.x + offset.x, coord.y + offset.y, coord.z + offset.z};
        const Chunk*     neighbor = store.try_get(neighbor_coord);
        if (neighbor != nullptr && neighbor->render_meta.has_water) {
            return true;
        }
    }
    return false;
}

bool chunk_requires_lod0_streaming_edge(const ChunkStore& store, ChunkCoord coord) {
    const Chunk* chunk = store.try_get(coord);
    if (chunk == nullptr) {
        return true;
    }
    if (store.is_pending_unload(coord)) {
        return true;
    }

    for (int section_idx = 0; section_idx < 8; ++section_idx) {
        const Section&     section       = chunk->sections[static_cast<size_t>(section_idx)];
        const glm::ivec3 section_coord = section_coord_from_index(section_idx);

        if (section.border.dirty) {
            return true;
        }

        for (int face_i = 0; face_i < 6; ++face_i) {
            const Face face = static_cast<Face>(face_i);
            ChunkCoord neighbor_chunk{};
            glm::ivec3 neighbor_section_coord{};
            neighbor_chunk_and_section(
                coord, section_coord, face, neighbor_chunk, neighbor_section_coord);

            if (neighbor_chunk.x == coord.x && neighbor_chunk.y == coord.y
                && neighbor_chunk.z == coord.z) {
                continue;
            }

            if (store.try_get(neighbor_chunk) == nullptr) {
                return true;
            }
            if (store.is_pending_unload(neighbor_chunk)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace engine