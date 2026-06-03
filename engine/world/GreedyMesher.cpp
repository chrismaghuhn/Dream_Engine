#include "engine/world/GreedyMesher.hpp"

#include "engine/gameplay/BlockRegistry.hpp"

#include <glm/glm.hpp>

#if defined(_MSC_VER)
#define ENGINE_MESH_NOINLINE __declspec(noinline)
#else
#define ENGINE_MESH_NOINLINE __attribute__((noinline))
#endif

namespace engine {
namespace {

struct VoxelSample {
    BlockState block{};
    uint8_t    sky_light   = 0;
    uint8_t    block_light = 0;
    bool       solid       = false;
};

inline uint8_t pack_light_nibbles(uint8_t sky, uint8_t block) {
    return static_cast<uint8_t>(((sky & 0xF) << 4) | (block & 0xF));
}

inline size_t border_face_index(int u, int v) {
    return static_cast<size_t>(u + SECTION_DIM * v);
}

ENGINE_MESH_NOINLINE const BorderCell& border_cell(
    const SectionBorderCache& border, Face face, int u, int v) {
    return border.face[static_cast<size_t>(face)][border_face_index(u, v)];
}

ENGINE_MESH_NOINLINE VoxelSample sample_voxel(const Section& section, int x, int y, int z) {
    if (x >= 0 && x < SECTION_DIM && y >= 0 && y < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const int idx = block_index(x, y, z);
        const BlockState block = section.read_block(x, y, z);
        return {
            block,
            section.sky_light[static_cast<size_t>(idx)],
            section.block_light[static_cast<size_t>(idx)],
            is_solid(block_id(block)),
        };
    }

    const SectionBorderCache& border = section.border;

    if (x == -1 && y >= 0 && y < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::NX, y, z);
        return {c.block, c.sky_light, c.block_light, is_solid(block_id(c.block))};
    }
    if (x == SECTION_DIM && y >= 0 && y < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::PX, y, z);
        return {c.block, c.sky_light, c.block_light, is_solid(block_id(c.block))};
    }
    if (y == -1 && x >= 0 && x < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::NY, x, z);
        return {c.block, c.sky_light, c.block_light, is_solid(block_id(c.block))};
    }
    if (y == SECTION_DIM && x >= 0 && x < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::PY, x, z);
        return {c.block, c.sky_light, c.block_light, is_solid(block_id(c.block))};
    }
    if (z == -1 && x >= 0 && x < SECTION_DIM && y >= 0 && y < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::NZ, x, y);
        return {c.block, c.sky_light, c.block_light, is_solid(block_id(c.block))};
    }
    if (z == SECTION_DIM && x >= 0 && x < SECTION_DIM && y >= 0 && y < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::PZ, x, y);
        return {c.block, c.sky_light, c.block_light, is_solid(block_id(c.block))};
    }

    return {};
}

ENGINE_MESH_NOINLINE VoxelSample sample_axis(
    const Section& section, int axis, int slice, int u, int v) {
    switch (axis) {
    case 0:
        return sample_voxel(section, slice, u, v);
    case 1:
        return sample_voxel(section, u, slice, v);
    default:
        return sample_voxel(section, u, v, slice);
    }
}

ENGINE_MESH_NOINLINE uint8_t corner_light(
    int x, int y, int z, const VoxelSample& solid_side, const Section& section) {
    uint8_t sky = solid_side.sky_light;
    uint8_t bl  = solid_side.block_light;

    const SectionBorderCache& border = section.border;

    if (x == 0 && y >= 0 && y < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::NX, y, z);
        sky = c.sky_light;
        bl  = c.block_light;
    }
    if (x == SECTION_DIM && y >= 0 && y < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::PX, y, z);
        sky = c.sky_light;
        bl  = c.block_light;
    }
    if (y == 0 && x >= 0 && x < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::NY, x, z);
        sky = c.sky_light;
        bl  = c.block_light;
    }
    if (y == SECTION_DIM && x >= 0 && x < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::PY, x, z);
        sky = c.sky_light;
        bl  = c.block_light;
    }
    if (z == 0 && x >= 0 && x < SECTION_DIM && y >= 0 && y < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::NZ, x, y);
        sky = c.sky_light;
        bl  = c.block_light;
    }
    if (z == SECTION_DIM && x >= 0 && x < SECTION_DIM && y >= 0 && y < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::PZ, x, y);
        sky = c.sky_light;
        bl  = c.block_light;
    }

    return pack_light_nibbles(sky, bl);
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

ENGINE_MESH_NOINLINE void emit_quad(
    int axis,
    bool positive,
    int slice,
    int u0,
    int v0,
    int width,
    int height,
    const VoxelSample& solid_side,
    const Section& section,
    std::vector<TerrainVertex>& vertices,
    std::vector<uint32_t>& indices) {
    const Face face = face_for_axis(axis, positive);

    glm::ivec3 corners[4];
    switch (axis) {
    case 0:
        corners[0] = {slice, u0, v0};
        corners[1] = {slice, u0 + height, v0};
        corners[2] = {slice, u0 + height, v0 + width};
        corners[3] = {slice, u0, v0 + width};
        break;
    case 1:
        corners[0] = {u0, slice, v0};
        corners[1] = {u0 + height, slice, v0};
        corners[2] = {u0 + height, slice, v0 + width};
        corners[3] = {u0, slice, v0 + width};
        break;
    default:
        corners[0] = {u0, v0, slice};
        corners[1] = {u0 + height, v0, slice};
        corners[2] = {u0 + height, v0 + width, slice};
        corners[3] = {u0, v0 + width, slice};
        break;
    }

    if (!positive) {
        std::swap(corners[1], corners[3]);
    }

    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const uint16_t material = block_id(solid_side.block);

    for (int i = 0; i < 4; ++i) {
        const glm::ivec3& c = corners[i];
        TerrainVertex vert{};
        vert.packed_position_normal = pack_vertex(
            static_cast<uint32_t>(c.x),
            static_cast<uint32_t>(c.y),
            static_cast<uint32_t>(c.z),
            face);
        vert.material_id = material;
        vert.ao            = 0;
        vert.light         = corner_light(c.x, c.y, c.z, solid_side, section);
        vertices.push_back(vert);
    }

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

uint32_t mask_key(const VoxelSample& solid_side) {
    return (static_cast<uint32_t>(block_id(solid_side.block)) << 16)
         | (static_cast<uint32_t>(solid_side.sky_light) << 8)
         | static_cast<uint32_t>(solid_side.block_light);
}

} // namespace

MeshSectionResult mesh_section(
    const Section& section,
    std::vector<TerrainVertex>& vertices,
    std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    std::vector<uint32_t> mask(static_cast<size_t>(SECTION_DIM * SECTION_DIM), 0u);

    for (int axis = 0; axis < 3; ++axis) {
        for (int positive = 0; positive < 2; ++positive) {
            for (int slice = 0; slice <= SECTION_DIM; ++slice) {
                int n = 0;
                for (int v = 0; v < SECTION_DIM; ++v) {
                    for (int u = 0; u < SECTION_DIM; ++u, ++n) {
                        const VoxelSample a = sample_axis(section, axis, slice - 1, u, v);
                        const VoxelSample b = sample_axis(section, axis, slice, u, v);

                        const bool emit_positive = a.solid && !b.solid;
                        const bool emit_negative = !a.solid && b.solid;

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
                for (int v = 0; v < SECTION_DIM; ++v) {
                    for (int u = 0; u < SECTION_DIM;) {
                        const uint32_t current = mask[static_cast<size_t>(n)];
                        if (current == 0) {
                            ++u;
                            ++n;
                            continue;
                        }

                        int width = 1;
                        while (u + width < SECTION_DIM
                               && mask[static_cast<size_t>(n + width)] == current) {
                            ++width;
                        }

                        int height = 1;
                        bool done  = false;
                        while (v + height < SECTION_DIM) {
                            for (int k = 0; k < width; ++k) {
                                if (mask[static_cast<size_t>(n + k + height * SECTION_DIM)]
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

                        VoxelSample solid_side{};
                        solid_side.block = make_block_state(BlockId(current >> 16), 0);
                        solid_side.sky_light =
                            static_cast<uint8_t>((current >> 8) & 0xFF);
                        solid_side.block_light =
                            static_cast<uint8_t>(current & 0xFF);
                        solid_side.solid = true;

                        emit_quad(
                            axis,
                            positive != 0,
                            slice,
                            u,
                            v,
                            width,
                            height,
                            solid_side,
                            section,
                            vertices,
                            indices);

                        for (int dv = 0; dv < height; ++dv) {
                            for (int du = 0; du < width; ++du) {
                                mask[static_cast<size_t>(n + du + dv * SECTION_DIM)] = 0u;
                            }
                        }

                        u += width;
                        n += width;
                    }
                }
            }
        }
    }

    return MeshSectionResult{vertices.size(), indices.size()};
}

} // namespace engine
