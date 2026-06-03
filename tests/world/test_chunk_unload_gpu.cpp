#include <catch2/catch_test_macros.hpp>

#include "engine/render/GpuDeferredFreeQueue.hpp"
#include "engine/render/GpuMeshPool.hpp"
#include "engine/render/StreamingTerrainSystem.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldEvents.hpp"
#include "engine/world/WorldModule.hpp"

TEST_CASE("chunk unload enqueues mesh slots for deferred GPU free") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);

    const engine::ChunkCoord coord{3, 0, -2};
    const engine::WorldConfig world_config{};
    const flecs::entity chunk_entity = engine::load_chunk(world, store, coord, world_config);
    REQUIRE(chunk_entity.is_alive());

    const engine::ChunkSlotRef before_ref = *chunk_entity.get<engine::ChunkSlotRef>();
    const std::uint32_t fake_slot = 91;
    const std::uint64_t frame_index = 12;

    engine::ChunkMeshSlots slots{};
    slots.section_slot_ids[0] = fake_slot;
    slots.section_slot_ids[4] = 102;
    chunk_entity.set<engine::ChunkMeshSlots>(slots);

    engine::GpuDeferredFreeQueue deferred_free(2);
    engine::GpuMeshPool mesh_pool;
    mesh_pool.init(VK_NULL_HANDLE, VK_NULL_HANDLE, 1024 * 1024, &deferred_free);
    REQUIRE(mesh_pool.is_live(fake_slot) == false);

    engine::ChunkGpuServices services{
        .deferred_free = &deferred_free,
        .frame_index = [&]() { return frame_index; },
    };
    engine::set_chunk_gpu_services(&services);

    engine::unload_chunk(world, store, coord);

    REQUIRE_FALSE(chunk_entity.is_alive());
    REQUIRE(store.try_get(coord) == nullptr);
    REQUIRE_FALSE(store.validate_slot_ref(before_ref));
    REQUIRE(deferred_free.pending_count() == 2);
    REQUIRE(deferred_free.pending()[0].slot_id == fake_slot);
    REQUIRE(deferred_free.pending()[0].last_used_frame == frame_index);
    REQUIRE(deferred_free.pending()[1].slot_id == 102);
    REQUIRE(mesh_pool.is_live(fake_slot) == false);
    REQUIRE(mesh_pool.is_live(102) == false);

    const flecs::entity reloaded = engine::load_chunk(world, store, coord, world_config);
    const engine::ChunkSlotRef after_ref = *reloaded.get<engine::ChunkSlotRef>();
    REQUIRE(after_ref.slot_id == before_ref.slot_id);
    REQUIRE(after_ref.generation > before_ref.generation);
}

TEST_CASE("pending unload chunk excluded from streaming terrain snapshot") {
    engine::ChunkStore store;
    store.init(8);

    const engine::ChunkCoord coord{0, 0, 0};
    store.allocate(coord);
    store.set_pending_unload(coord, true);

    engine::WorldRenderSnapshot snapshot{};
    snapshot.view = glm::mat4(1.f);
    snapshot.proj = glm::mat4(1.f);

    engine::StreamingTerrainSystem streaming;
    streaming.build_snapshot(snapshot, glm::vec3(0.f), store);

    REQUIRE(snapshot.opaque_sections.empty());
}
