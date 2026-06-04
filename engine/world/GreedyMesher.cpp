#include "engine/world/GreedyMesher.hpp"

#include "engine/gameplay/BlockRegistry.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <utility>

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
    bool       opaque_solid = false;
    bool       water        = false;
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
        const BlockId id = block_id(block);
        return {
            block,
            section.sky_light[static_cast<size_t>(idx)],
            section.block_light[static_cast<size_t>(idx)],
            is_solid(id),
            is_water(id),
        };
    }

    const SectionBorderCache& border = section.border;

    if (x == -1 && y >= 0 && y < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::NX, y, z);
        const BlockId id = block_id(c.block);
        return {c.block, c.sky_light, c.block_light, is_solid(id), is_water(id)};
    }
    if (x == SECTION_DIM && y >= 0 && y < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::PX, y, z);
        const BlockId id = block_id(c.block);
        return {c.block, c.sky_light, c.block_light, is_solid(id), is_water(id)};
    }
    if (y == -1 && x >= 0 && x < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::NY, x, z);
        const BlockId id = block_id(c.block);
        return {c.block, c.sky_light, c.block_light, is_solid(id), is_water(id)};
    }
    if (y == SECTION_DIM && x >= 0 && x < SECTION_DIM && z >= 0 && z < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::PY, x, z);
        const BlockId id = block_id(c.block);
        return {c.block, c.sky_light, c.block_light, is_solid(id), is_water(id)};
    }
    if (z == -1 && x >= 0 && x < SECTION_DIM && y >= 0 && y < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::NZ, x, y);
        const BlockId id = block_id(c.block);
        return {c.block, c.sky_light, c.block_light, is_solid(id), is_water(id)};
    }
    if (z == SECTION_DIM && x >= 0 && x < SECTION_DIM && y >= 0 && y < SECTION_DIM) {
        const BorderCell& c = border_cell(border, Face::PZ, x, y);
        const BlockId id = block_id(c.block);
        return {c.block, c.sky_light, c.block_light, is_solid(id), is_water(id)};
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
    int x, int y, int z, const VoxelSample& face_side, const Section& section) {
    uint8_t sky = face_side.sky_light;
    uint8_t bl  = face_side.block_light;

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

struct TangentBasis {
    glm::ivec3 air_step{0};
    glm::ivec3 eu{0};
    glm::ivec3 ev{0};
};

ENGINE_MESH_NOINLINE TangentBasis tangent_basis_for_axis(int axis, bool positive) {
    TangentBasis b{};
    switch (axis) {
    case 0:
        b.eu       = {0, 1, 0};
        b.ev       = {0, 0, 1};
        b.air_step = {positive ? 1 : -1, 0, 0};
        break;
    case 1:
        b.eu       = {1, 0, 0};
        b.ev       = {0, 0, 1};
        b.air_step = {0, positive ? 1 : -1, 0};
        break;
    default:
        b.eu       = {1, 0, 0};
        b.ev       = {0, 1, 0};
        b.air_step = {0, 0, positive ? 1 : -1};
        break;
    }
    return b;
}

ENGINE_MESH_NOINLINE glm::ivec3 add_ivec3(glm::ivec3 a, glm::ivec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

ENGINE_MESH_NOINLINE glm::ivec3 scale_ivec3(glm::ivec3 v, int s) {
    return {v.x * s, v.y * s, v.z * s};
}

ENGINE_MESH_NOINLINE bool occludes_ao(const VoxelSample& s) {
    return s.opaque_solid;
}

ENGINE_MESH_NOINLINE uint8_t corner_ao(int axis,
                                       bool positive,
                                       int x,
                                       int y,
                                       int z,
                                       int du_sign,
                                       int dv_sign,
                                       const Section& section) {
    // Vertices sit on the air-side of the face; step back into the solid voxel first.
    const TangentBasis b  = tangent_basis_for_axis(axis, positive);
    const glm::ivec3 base = add_ivec3({x, y, z}, scale_ivec3(b.air_step, -1));
    const glm::ivec3 s1   = add_ivec3(base, scale_ivec3(b.eu, du_sign));
    const glm::ivec3 s2   = add_ivec3(base, scale_ivec3(b.ev, dv_sign));
    const glm::ivec3 c =
        add_ivec3(add_ivec3(base, scale_ivec3(b.eu, du_sign)), scale_ivec3(b.ev, dv_sign));

    const int a  = occludes_ao(sample_voxel(section, s1.x, s1.y, s1.z)) ? 1 : 0;
    const int b2 = occludes_ao(sample_voxel(section, s2.x, s2.y, s2.z)) ? 1 : 0;
    const int c2 = occludes_ao(sample_voxel(section, c.x, c.y, c.z)) ? 1 : 0;
    const int occl = std::max(c2, a * b2);
    return static_cast<uint8_t>(std::clamp(3 - (a + b2 + occl), 0, 3));
}

ENGINE_MESH_NOINLINE void emit_quad(
    int axis,
    bool positive,
    int slice,
    int u0,
    int v0,
    int width,
    int height,
    const VoxelSample& face_side,
    const Section& section,
    bool is_water_layer,
    std::vector<TerrainVertex>& vertices,
    std::vector<uint32_t>& indices) {
    const Face face = face_for_axis(axis, positive);

    // The merged quad spans [u0, u0 + width] along U and [v0, v0 + height]
    // along V. `width` grows along the inner (u) scan and `height` along the
    // outer (v) scan, so U corners advance by `width` and V corners by `height`.
    glm::ivec3 corners[4];
    switch (axis) {
    case 0: // X slice: U -> y, V -> z
        corners[0] = {slice, u0, v0};
        corners[1] = {slice, u0 + width, v0};
        corners[2] = {slice, u0 + width, v0 + height};
        corners[3] = {slice, u0, v0 + height};
        break;
    case 1: // Y slice: U -> x, V -> z
        corners[0] = {u0, slice, v0};
        corners[1] = {u0 + width, slice, v0};
        corners[2] = {u0 + width, slice, v0 + height};
        corners[3] = {u0, slice, v0 + height};
        break;
    default: // Z slice: U -> x, V -> y
        corners[0] = {u0, v0, slice};
        corners[1] = {u0 + width, v0, slice};
        corners[2] = {u0 + width, v0 + height, slice};
        corners[3] = {u0, v0 + height, slice};
        break;
    }

    // Winding: axis 1 (Y) positive faces (sky/top) need the opposite swap from X/Z.
    if (axis == 1) {
        if (positive) {
            std::swap(corners[1], corners[3]);
        }
    } else if (!positive) {
        std::swap(corners[1], corners[3]);
    }

    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const uint16_t material = block_id(face_side.block);

    auto tangent_uv = [&](const glm::ivec3& c) {
        int tu = 0;
        int tv = 0;
        switch (axis) {
        case 0:
            tu = c.y;
            tv = c.z;
            break;
        case 1:
            tu = c.x;
            tv = c.z;
            break;
        default:
            tu = c.x;
            tv = c.y;
            break;
        }
        const int du_sign = (tu == u0 + width) ? 1 : -1;
        const int dv_sign = (tv == v0 + height) ? 1 : -1;
        return std::pair{du_sign, dv_sign};
    };

    for (int i = 0; i < 4; ++i) {
        const glm::ivec3& c = corners[i];
        const auto [du_sign, dv_sign] = tangent_uv(c);
        TerrainVertex vert{};
        vert.packed_position_normal = pack_vertex(
            static_cast<uint32_t>(c.x),
            static_cast<uint32_t>(c.y),
            static_cast<uint32_t>(c.z),
            face);
        vert.material_id = material;
        vert.ao            = is_water_layer
                                 ? static_cast<uint8_t>(3)
                                 : corner_ao(axis, positive, c.x, c.y, c.z, du_sign, dv_sign, section);
        vert.light         = corner_light(c.x, c.y, c.z, face_side, section);
        vertices.push_back(vert);
    }

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

uint32_t mask_key(const VoxelSample& face_side) {
    return (static_cast<uint32_t>(block_id(face_side.block)) << 16)
         | (static_cast<uint32_t>(face_side.sky_light) << 8)
         | static_cast<uint32_t>(face_side.block_light);
}

using FacePredicate = bool (*)(const VoxelSample&);

bool opaque_face_predicate(const VoxelSample& sample) {
    return sample.opaque_solid;
}

bool water_face_predicate(const VoxelSample& sample) {
    return sample.water;
}

ENGINE_MESH_NOINLINE void mesh_section_layer(
    const Section& section,
    FacePredicate face_predicate,
    bool is_water_layer,
    std::vector<TerrainVertex>& vertices,
    std::vector<uint32_t>& indices) {
    std::vector<uint32_t> mask(static_cast<size_t>(SECTION_DIM * SECTION_DIM), 0u);

    for (int axis = 0; axis < 3; ++axis) {
        for (int positive = 0; positive < 2; ++positive) {
            for (int slice = 0; slice <= SECTION_DIM; ++slice) {
                int n = 0;
                for (int v = 0; v < SECTION_DIM; ++v) {
                    for (int u = 0; u < SECTION_DIM; ++u, ++n) {
                        const VoxelSample a = sample_axis(section, axis, slice - 1, u, v);
                        const VoxelSample b = sample_axis(section, axis, slice, u, v);

                        const bool emit_positive =
                            face_predicate(a) && !face_predicate(b);
                        const bool emit_negative =
                            !face_predicate(a) && face_predicate(b);

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

                        VoxelSample face_side{};
                        face_side.block = make_block_state(BlockId(current >> 16), 0);
                        face_side.sky_light =
                            static_cast<uint8_t>((current >> 8) & 0xFF);
                        face_side.block_light =
                            static_cast<uint8_t>(current & 0xFF);
                        const BlockId face_id = block_id(face_side.block);
                        face_side.opaque_solid = is_solid(face_id);
                        face_side.water = is_water(face_id);

                        emit_quad(
                            axis,
                            positive != 0,
                            slice,
                            u,
                            v,
                            width,
                            height,
                            face_side,
                            section,
                            is_water_layer,
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
}

} // namespace

MeshSectionResult mesh_section(
    const Section& section,
    std::vector<TerrainVertex>& opaque_vertices,
    std::vector<uint32_t>& opaque_indices,
    std::vector<TerrainVertex>& water_vertices,
    std::vector<uint32_t>& water_indices) {
    opaque_vertices.clear();
    opaque_indices.clear();
    water_vertices.clear();
    water_indices.clear();

    mesh_section_layer(section, opaque_face_predicate, false, opaque_vertices, opaque_indices);
    mesh_section_layer(section, water_face_predicate, true, water_vertices, water_indices);

    return MeshSectionResult{
        opaque_vertices.size(),
        opaque_indices.size(),
        water_vertices.size(),
        water_indices.size(),
    };
}

} // namespace engine
