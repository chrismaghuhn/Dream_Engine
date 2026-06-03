#include <catch2/catch_test_macros.hpp>

#include "engine/core/JobSystem.hpp"

#define private public
#include "engine/render/StreamingTerrainSystem.hpp"
#undef private

#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldModule.hpp"

#include <glm/glm.hpp>

TEST_CASE("EvtChunkLoaded observer schedules terrain mesh jobs") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(32);

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 2, .io_threads = 1, .meshing_threads = 2});

    const engine::WorldConfig world_config{};
    engine::StreamingTerrainSystem streaming;
    streaming.init(world, store, jobs, world_config);
    streaming.register_observers(world);

    // Chunk y=0 is world y 0-31 (solid stone underground), so all sections
    // have geometry and count_mesh_ready_sections() returns > 0 after meshing.
    const engine::ChunkCoord coord{0, 0, 0};
    const flecs::entity entity = engine::load_chunk(world, store, coord, world_config);
    REQUIRE(entity.is_alive());

    // warmup_meshes_near_focus drives the full mesh pipeline:
    // process_mesh_backlog() schedules jobs, then wait+drain in a loop sets
    // mesh_ready. This is the production-proven path (also used in Engine::startup).
    streaming.warmup_meshes_near_focus(jobs, glm::vec3{0.f}, 1);

    REQUIRE(streaming.count_mesh_ready_sections() > 0);

    jobs.shutdown();
}

TEST_CASE("mesh completion keeps stale slot when no active replacement exists") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(4);

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 1, .io_threads = 1, .meshing_threads = 1});

    engine::StreamingTerrainSystem streaming;
    streaming.init(world, store, jobs, engine::WorldConfig{});

    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(store.allocate(coord) != nullptr);

    engine::StreamingTerrainSystem::ChunkMeshState& chunk_state =
        streaming.chunk_meshes_[coord];
    chunk_state.coord = coord;
    engine::StreamingTerrainSystem::SectionMeshState& section_state =
        chunk_state.sections[0];
    section_state.section_index = 0;
    section_state.mesh_schedule_serial = 7;
    section_state.stale_opaque_gpu_slot_id = 91;
    section_state.stale_opaque_draw_index_count = 36;

    engine::StreamingTerrainSystem::MeshCompletion completion{};
    completion.coord = coord;
    completion.section_index = 0;
    completion.schedule_serial = 7;
    completion.opaque_vertices.resize(4);
    completion.opaque_indices = {0, 1, 2, 2, 3, 0};
    streaming.completions_.push_back(std::move(completion));

    streaming.jobs_ = nullptr;
    engine::GpuDeferredFreeQueue deferred_free(3);
    engine::GpuMeshPool mesh_pool;
    mesh_pool.init(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, &deferred_free);
    engine::MeshUploadQueue upload_queue(2, 1024 * 1024);

    streaming.on_frame(glm::vec3{0.f}, 0, mesh_pool, upload_queue, deferred_free);

    REQUIRE(section_state.stale_opaque_gpu_slot_id == 91);
    REQUIRE(section_state.stale_opaque_draw_index_count == 36);
    REQUIRE(streaming.pending_slot_frees_.empty());

    jobs.shutdown();
}
