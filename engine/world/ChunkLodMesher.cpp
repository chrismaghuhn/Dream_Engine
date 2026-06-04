#include "engine/world/ChunkLodMesher.hpp"

#include "engine/gameplay/BlockRegistry.hpp"

#include <algorithm>
#include <array>
#include <glm/glm.hpp>

namespace engine {
namespace {

constexpr int COARSE_DIM = SECTION_DIM;

struct CoarseCell {
    bool       opaque       = false;
    BlockState block{};
    uint8_t    sky_light    = 0;
    uint8_t    block_light  = 0;
};

inline uint8_t pack_light_nibbles(uint8_t sky, uint8_t block) {
    return static_cast<uint8_t>(((sky & 0xF) << 4) | (block & 0xF));
}

inline int fine_index(int fx, int fy, int fz) {
    return block_index(fx & 15, fy & 15, fz & 15);
}

CoarseCell sample_fine(const Chunk& chunk, int fx, int fy, int fz) {
    const glm::ivec3 sec = {fx >> 4, fy >> 4, fz >> 4};
    const glm::ivec3 blk = {fx & 15, fy & 15, fz & 15};
    const Section&     section = chunk.section_at(sec);
    const int          idx     = fine_index(fx, fy, fz);
    const BlockState   block   = section.read_block(blk.x, blk.y, blk.z);
    const BlockId      id      = block_id(block);
    return {
        is_solid(id),
        block,
        section.sky_light[static_cast<size_t>(idx)],
        section.block_light[static_cast<size_t>(idx)],
    };
}

CoarseCell downsample_coarse_cell(const Chunk& chunk, int gx, int gy, int gz) {
    CoarseCell result{};
    int        sky_sum   = 0;
    int        block_sum = 0;
    int        count     = 0;

    for (int dz = 0; dz < 2; ++dz) {
        for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
                const int      fx = gx * 2 + dx;
                const int      fy = gy * 2 + dy;
                const int      fz = gz * 2 + dz;
                const CoarseCell fine = sample_fine(chunk, fx, fy, fz);
                sky_sum += fine.sky_light;
                block_sum += fine.block_light;
                ++count;
                if (fine.opaque) {
                    result.opaque = true;
                    if (result.block.raw == 0) {
                        result.block = fine.block;
                    }
                }
            }
        }
    }

    result.sky_light =
        static_cast<uint8_t>((sky_sum + count / 2) / std::max(count, 1));
    result.block_light =
        static_cast<uint8_t>((block_sum + count / 2) / std::max(count, 1));
    return result;
}

Face face_for_axis(int axis, bool positive) {
    switch (axis) {
    case 0:
        return positive ? Face::PX : Face::NX;
    case 1:
        return positive ? Face::PY : Face::NY;
    default:
        return positive ? Face::PZ : Face::NZ;
    }
}

void emit_quad(
    int axis,
    bool positive,
    int slice,
    int u0,
    int v0,
    int width,
    int height,
    const CoarseCell& face_side,
    std::vector<TerrainVertex>& vertices,
    std::vector<uint32_t>& indices) {
    const Face face = face_for_axis(axis, positive);

    glm::ivec3 corners[4];
    switch (axis) {
    case 0:
        corners[0] = {slice, u0, v0};
        corners[1] = {slice, u0 + width, v0};
        corners[2] = {slice, u0 + width, v0 + height};
        corners[3] = {slice, u0, v0 + height};
        break;
    case 1:
        corners[0] = {u0, slice, v0};
        corners[1] = {u0 + width, slice, v0};
        corners[2] = {u0 + width, slice, v0 + height};
        corners[3] = {u0, slice, v0 + height};
        break;
    default:
        corners[0] = {u0, v0, slice};
        corners[1] = {u0 + width, v0, slice};
        corners[2] = {u0 + width, v0 + height, slice};
        corners[3] = {u0, v0 + height, slice};
        break;
    }

    if (axis == 1) {
        if (positive) {
            std::swap(corners[1], corners[3]);
        }
    } else if (!positive) {
        std::swap(corners[1], corners[3]);
    }

    const uint32_t base      = static_cast<uint32_t>(vertices.size());
    const uint16_t material  = block_id(face_side.block);
    const uint8_t  light     = pack_light_nibbles(face_side.sky_light, face_side.block_light);

    for (int i = 0; i < 4; ++i) {
        const glm::ivec3& c = corners[i];
        TerrainVertex     vert{};
        vert.packed_position_normal = pack_vertex(
            static_cast<uint32_t>(c.x),
            static_cast<uint32_t>(c.y),
            static_cast<uint32_t>(c.z),
            face);
        vert.material_id = material;
        vert.ao          = 3;
        vert.light       = light;
        vertices.push_back(vert);
    }

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

uint32_t mask_key(const CoarseCell& cell) {
    return (static_cast<uint32_t>(block_id(cell.block)) << 16)
         | (static_cast<uint32_t>(cell.sky_light) << 8)
         | static_cast<uint32_t>(cell.block_light);
}

CoarseCell sample_coarse_axis(
    const std::array<CoarseCell, COARSE_DIM * COARSE_DIM * COARSE_DIM>& grid,
    int axis,
    int slice,
    int u,
    int v) {
    if (slice < 0 || slice >= COARSE_DIM) {
        return {};
    }
    switch (axis) {
    case 0:
        return grid[static_cast<size_t>(block_index(slice, u, v))];
    case 1:
        return grid[static_cast<size_t>(block_index(u, slice, v))];
    default:
        return grid[static_cast<size_t>(block_index(u, v, slice))];
    }
}

bool coarse_opaque_predicate(const CoarseCell& cell) {
    return cell.opaque;
}

void mesh_coarse_layer(
    const std::array<CoarseCell, COARSE_DIM * COARSE_DIM * COARSE_DIM>& grid,
    std::vector<TerrainVertex>& vertices,
    std::vector<uint32_t>& indices) {
    std::vector<uint32_t> mask(static_cast<size_t>(COARSE_DIM * COARSE_DIM), 0u);

    for (int axis = 0; axis < 3; ++axis) {
        for (int positive = 0; positive < 2; ++positive) {
            for (int slice = 0; slice <= COARSE_DIM; ++slice) {
                int n = 0;
                for (int v = 0; v < COARSE_DIM; ++v) {
                    for (int u = 0; u < COARSE_DIM; ++u, ++n) {
                        const CoarseCell a = sample_coarse_axis(grid, axis, slice - 1, u, v);
                        const CoarseCell b = sample_coarse_axis(grid, axis, slice, u, v);

                        const bool emit_positive =
                            coarse_opaque_predicate(a) && !coarse_opaque_predicate(b);
                        const bool emit_negative =
                            !coarse_opaque_predicate(a) && coarse_opaque_predicate(b);

                        if (positive) {
                            mask[static_cast<size_t>(n)] =
                                emit_positive ? mask_key(a) : 0u;
                        } else {
                            mask[static_cast<size_t>(n)] =
                                emit_negative ? mask_key(b) : 0u;
                        }
                    }
                }

                n = 0;
                for (int v = 0; v < COARSE_DIM; ++v) {
                    for (int u = 0; u < COARSE_DIM;) {
                        const uint32_t current = mask[static_cast<size_t>(n)];
                        if (current == 0) {
                            ++u;
                            ++n;
                            continue;
                        }

                        int width = 1;
                        while (u + width < COARSE_DIM
                               && mask[static_cast<size_t>(n + width)] == current) {
                            ++width;
                        }

                        int height = 1;
                        bool done  = false;
                        while (v + height < COARSE_DIM) {
                            for (int k = 0; k < width; ++k) {
                                if (mask[static_cast<size_t>(n + k + height * COARSE_DIM)]
                                    != current) {
                                    done = true;
                                    break;
                                }
                            }
                            if (done) {
                                break;
                            }
                            ++height;
                        }

                        CoarseCell face_side{};
                        face_side.block = make_block_state(BlockId(current >> 16), 0);
                        face_side.sky_light =
                            static_cast<uint8_t>((current >> 8) & 0xFF);
                        face_side.block_light =
                            static_cast<uint8_t>(current & 0xFF);
                        face_side.opaque = true;

                        emit_quad(
                            axis,
                            positive != 0,
                            slice,
                            u,
                            v,
                            width,
                            height,
                            face_side,
                            vertices,
                            indices);

                        for (int dv = 0; dv < height; ++dv) {
                            for (int du = 0; du < width; ++du) {
                                mask[static_cast<size_t>(n + du + dv * COARSE_DIM)] = 0u;
                            }
                        }

                        u += width;
                        n += width;
                    }
                }
            }
        }
    }
}

} // namespace

MeshChunkLodResult mesh_chunk_lod1(
    const Chunk& chunk,
    std::vector<TerrainVertex>& opaque_vertices,
    std::vector<uint32_t>& opaque_indices) {
    opaque_vertices.clear();
    opaque_indices.clear();

    std::array<CoarseCell, COARSE_DIM * COARSE_DIM * COARSE_DIM> grid{};
    bool any_opaque = false;
    for (int gy = 0; gy < COARSE_DIM; ++gy) {
        for (int gz = 0; gz < COARSE_DIM; ++gz) {
            for (int gx = 0; gx < COARSE_DIM; ++gx) {
                const CoarseCell cell = downsample_coarse_cell(chunk, gx, gy, gz);
                grid[static_cast<size_t>(block_index(gx, gy, gz))] = cell;
                any_opaque = any_opaque || cell.opaque;
            }
        }
    }

    if (!any_opaque) {
        return {};
    }

    mesh_coarse_layer(grid, opaque_vertices, opaque_indices);

    return MeshChunkLodResult{
        opaque_vertices.size(),
        opaque_indices.size(),
        0,
        0,
    };
}

} // namespace engine