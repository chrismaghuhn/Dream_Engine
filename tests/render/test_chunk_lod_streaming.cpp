#include <catch2/catch_test_macros.hpp>

#include "engine/core/JobSystem.hpp"

#define private public
#include "engine/render/StreamingTerrainSystem.hpp"
#undef private

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/SectionIndexing.hpp"
#include "engine/world/TerrainLod.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldModule.hpp"

namespace {

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
    engine::recompute_chunk_render_meta(chunk);
}

} // namespace

TEST_CASE("schedule_chunk_lod1_mesh completes at lod1 distance") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(32);

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 1, .io_threads = 1, .meshing_threads = 2});

    engine::StreamingTerrainSystem streaming;
    streaming.init(world, store, jobs, engine::WorldConfig{});

    const engine::ChunkCoord coord{5, 0, 0};
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                engine::Chunk* neighbor =
                    store.allocate({coord.x + dx, coord.y + dy, coord.z + dz});
                REQUIRE(neighbor != nullptr);
                fill_chunk_stone(*neighbor);
            }
        }
    }
    engine::Chunk* chunk = store.try_get(coord);
    REQUIRE(chunk != nullptr);
    engine::refresh_chunk_section_borders(store, coord);
    engine::refresh_loaded_chunk_neighbors(store, coord);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                engine::Chunk* loaded = store.try_get({coord.x + dx, coord.y + dy, coord.z + dz});
                if (loaded == nullptr) {
                    continue;
                }
                for (engine::Section& section : loaded->sections) {
                    section.border.dirty = false;
                }
            }
        }
    }

    const glm::vec3 focus{0.f, 16.f, 0.f};
    streaming.focus_world_ = focus;

    REQUIRE(streaming.select_chunk_lod_for_coord(coord) == engine::TerrainLodLevel::Lod1);
    REQUIRE_FALSE(engine::chunk_force_lod0_water_border(store, coord));
    REQUIRE_FALSE(engine::chunk_requires_lod0_streaming_edge(store, coord));

    streaming.schedule_chunk_lod1_mesh(coord);
    REQUIRE(streaming.count_pending_lod1_mesh_jobs() >= 1);

    for (int attempt = 0; attempt < 40; ++attempt) {
        streaming.sync_mesh_workers();
        if (streaming.chunk_meshes_[coord].lod1.mesh_ready) {
            break;
        }
    }

    const engine::StreamingTerrainSystem::ChunkLod1MeshState& lod1 =
        streaming.chunk_meshes_[coord].lod1;
    REQUIRE(lod1.mesh_ready);
    REQUIRE_FALSE(lod1.mesh_job_pending);
    REQUIRE(lod1.opaque_indices.size() > 0);

    jobs.shutdown();
}