#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/core/JobSystem.hpp"

#define private public
#include "engine/render/GpuMeshPool.hpp"
#include "engine/render/StreamingTerrainSystem.hpp"
#undef private

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/BlockLight.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/SectionIndexing.hpp"
#include "engine/world/TerrainLod.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldModule.hpp"

namespace {

constexpr std::uint32_t kFakeGpuSlot = 7;

void fill_chunk_stone(engine::Chunk& chunk) {
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (engine::Section& section : chunk.sections) {
        for (int y = 0; y < engine::SECTION_DIM; ++y) {
            for (int z = 0; z < engine::SECTION_DIM; ++z) {
                for (int x = 0; x < engine::SECTION_DIM; ++x) {
                    REQUIRE(section.write_block(x, y, z, stone));
                }
            }
        }
        section.sync_occupancy_from_blocks();
    }
}

void carve_face_air(engine::Section& section, engine::Face face) {
    const engine::BlockState air = engine::make_block_state(engine::BLOCK_AIR, 0);
    const int axis = static_cast<int>(face) / 2;
    const int fixed = (static_cast<int>(face) & 1) != 0 ? 0 : engine::SECTION_DIM - 1;

    for (int y = 0; y < engine::SECTION_DIM; ++y) {
        for (int z = 0; z < engine::SECTION_DIM; ++z) {
            for (int x = 0; x < engine::SECTION_DIM; ++x) {
                glm::ivec3 pos{x, y, z};
                if (axis == 0) {
                    pos.x = fixed;
                } else if (axis == 1) {
                    pos.y = fixed;
                } else {
                    pos.z = fixed;
                }
                REQUIRE(section.write_block(pos.x, pos.y, pos.z, air));
            }
        }
    }
    section.sync_occupancy_from_blocks();
}

engine::GpuMeshPool make_live_mesh_pool() {
    engine::GpuMeshPool mesh_pool;
    mesh_pool.init(VK_NULL_HANDLE, VK_NULL_HANDLE, 1024 * 1024, nullptr);
    engine::GpuMeshSlot slot{};
    slot.slot_id = kFakeGpuSlot;
    slot.live = true;
    slot.vertex_buffer = reinterpret_cast<VkBuffer>(1);
    slot.index_buffer = reinterpret_cast<VkBuffer>(2);
    mesh_pool.slots_.push_back(slot);
    return mesh_pool;
}

void mark_section_drawable(engine::StreamingTerrainSystem& streaming,
                           const engine::ChunkCoord coord,
                           const std::uint8_t section_index) {
    engine::StreamingTerrainSystem::ChunkMeshState& chunk_state = streaming.chunk_meshes_[coord];
    chunk_state.coord = coord;
    engine::StreamingTerrainSystem::SectionMeshState& section_state =
        chunk_state.sections[section_index];
    section_state.section_index = section_index;
    section_state.mesh_ready = true;
    section_state.opaque_gpu_allocated = true;
    section_state.opaque_gpu_uploaded = true;
    section_state.opaque_draw_index_count = 96;
    section_state.opaque_gpu_slot_id = kFakeGpuSlot;
    section_state.empty_skip = false;
    section_state.occluded_skip = false;
}

engine::WorldRenderSnapshot make_inclusive_snapshot() {
    engine::WorldRenderSnapshot snapshot{};
    snapshot.view = glm::mat4(1.f);
    snapshot.proj = glm::ortho(-512.f, 512.f, -512.f, 512.f, -512.f, 512.f);
    return snapshot;
}

void load_neighbor_shell(engine::ChunkStore& store, const engine::ChunkCoord center) {
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                const engine::ChunkCoord coord{center.x + dx, center.y + dy, center.z + dz};
                engine::Chunk* chunk = store.try_get(coord);
                if (chunk == nullptr) {
                    chunk = store.allocate(coord);
                }
                REQUIRE(chunk != nullptr);
                fill_chunk_stone(*chunk);
            }
        }
    }
    engine::refresh_chunk_section_borders(store, center);
    engine::refresh_loaded_chunk_neighbors(store, center);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                engine::Chunk* loaded =
                    store.try_get({center.x + dx, center.y + dy, center.z + dz});
                if (loaded == nullptr) {
                    continue;
                }
                for (engine::Section& section : loaded->sections) {
                    section.border.dirty = false;
                }
            }
        }
    }
}

bool snapshot_has_opaque_section(const engine::WorldRenderSnapshot& snapshot,
                                 const engine::ChunkCoord coord,
                                 const std::uint8_t section_index) {
    for (const engine::DrawSection& draw : snapshot.opaque_sections) {
        if (draw.coord == coord && draw.section_index == section_index
            && draw.lod_level == engine::TerrainLodLevel::Lod0) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("build_snapshot omits disconnected buried section when BFS active") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(64);
    const engine::ChunkCoord coord{0, 0, 0};
    load_neighbor_shell(store, coord);
    REQUIRE_FALSE(engine::chunk_requires_lod0_streaming_edge(store, coord));

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 1, .io_threads = 1, .meshing_threads = 1});

    engine::StreamingTerrainSystem streaming;
    engine::TerrainOcclusionConfig occlusion{};
    occlusion.enabled = true;
    occlusion.max_bfs_sections = 8192;
    streaming.init(world, store, jobs, engine::WorldConfig{}, {}, occlusion);

    const glm::vec3 focus{8.f, 8.f, 8.f};
    streaming.focus_world_ = focus;
    mark_section_drawable(streaming, coord, 0);
    mark_section_drawable(streaming, coord, 1);

    engine::GpuMeshPool mesh_pool = make_live_mesh_pool();
    engine::WorldRenderSnapshot snapshot = make_inclusive_snapshot();
    streaming.build_snapshot(snapshot, glm::vec3(0.f), store, mesh_pool);

    REQUIRE(streaming.count_connectivity_culled_sections() >= 1);
    REQUIRE(snapshot_has_opaque_section(snapshot, coord, 0));
    REQUIRE_FALSE(snapshot_has_opaque_section(snapshot, coord, 1));

    jobs.shutdown();
}

TEST_CASE("build_snapshot draws disconnected section on streaming-edge chunk") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(8);
    engine::Chunk* chunk = store.allocate({0, 0, 0});
    REQUIRE(chunk != nullptr);
    fill_chunk_stone(*chunk);
    REQUIRE(engine::chunk_requires_lod0_streaming_edge(store, {0, 0, 0}));

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 1, .io_threads = 1, .meshing_threads = 1});

    engine::StreamingTerrainSystem streaming;
    engine::TerrainOcclusionConfig occlusion{};
    occlusion.enabled = true;
    streaming.init(world, store, jobs, engine::WorldConfig{}, {}, occlusion);

    const glm::vec3 focus{8.f, 8.f, 8.f};
    streaming.focus_world_ = focus;
    mark_section_drawable(streaming, {0, 0, 0}, 0);
    mark_section_drawable(streaming, {0, 0, 0}, 1);

    engine::GpuMeshPool mesh_pool = make_live_mesh_pool();
    engine::WorldRenderSnapshot snapshot = make_inclusive_snapshot();
    streaming.build_snapshot(snapshot, glm::vec3(0.f), store, mesh_pool);

    REQUIRE(snapshot_has_opaque_section(snapshot, {0, 0, 0}, 0));
    REQUIRE(snapshot_has_opaque_section(snapshot, {0, 0, 0}, 1));

    jobs.shutdown();
}

TEST_CASE("build_snapshot draws disconnected section when BFS truncated") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(8);
    engine::Chunk* chunk = store.allocate({0, 0, 0});
    REQUIRE(chunk != nullptr);
    fill_chunk_stone(*chunk);
    carve_face_air(chunk->section_at({0, 0, 0}), engine::Face::PX);
    carve_face_air(chunk->section_at({1, 0, 0}), engine::Face::NX);

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 1, .io_threads = 1, .meshing_threads = 1});

    engine::StreamingTerrainSystem streaming;
    engine::TerrainOcclusionConfig occlusion{};
    occlusion.enabled = true;
    occlusion.max_bfs_sections = 1;
    streaming.init(world, store, jobs, engine::WorldConfig{}, {}, occlusion);

    const glm::vec3 focus{8.f, 8.f, 8.f};
    streaming.focus_world_ = focus;
    mark_section_drawable(streaming, {0, 0, 0}, 0);
    mark_section_drawable(streaming, {0, 0, 0}, 1);

    engine::GpuMeshPool mesh_pool = make_live_mesh_pool();
    engine::WorldRenderSnapshot snapshot = make_inclusive_snapshot();
    streaming.build_snapshot(snapshot, glm::vec3(0.f), store, mesh_pool);

    REQUIRE(streaming.connectivity_bfs_truncated());
    REQUIRE(snapshot_has_opaque_section(snapshot, {0, 0, 0}, 1));

    jobs.shutdown();
}