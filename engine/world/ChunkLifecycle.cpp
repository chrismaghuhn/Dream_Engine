#include "engine/world/ChunkLifecycle.hpp"

#include <spdlog/spdlog.h>

namespace engine {

void register_chunk_lifecycle(flecs::world& world) {
    world.component<ChunkCoord>();
    world.component<ChunkSlotRef>();

    world.observer<ChunkSlotRef>()
        .event(flecs::OnRemove)
        .each([](flecs::entity e, ChunkSlotRef slot_ref) {
            const ChunkCoord* coord = e.get<ChunkCoord>();
            if (!coord) {
                return;
            }
            SPDLOG_DEBUG(
                "on_chunk_unload coord=({},{},{}) slot_id={} generation={} enqueue_free stub",
                coord->x,
                coord->y,
                coord->z,
                slot_ref.slot_id,
                slot_ref.generation);
        });

    world.observer<ChunkDirty>()
        .event(flecs::OnAdd)
        .each([](flecs::entity e, ChunkDirty) {
            const ChunkCoord* coord = e.get<ChunkCoord>();
            if (!coord) {
                SPDLOG_DEBUG("ChunkDirty entity={}", e.id());
                return;
            }
            SPDLOG_DEBUG(
                "ChunkDirty chunk=({},{},{}) entity={}",
                coord->x,
                coord->y,
                coord->z,
                e.id());
        });
}

flecs::entity load_chunk(flecs::world& world, ChunkStore& store, ChunkCoord coord) {
    const uint64_t existing = store.entity_for(coord);
    if (existing != 0 && store.try_get(coord) != nullptr) {
        return world.entity(existing);
    }

    Chunk* chunk = store.allocate(coord);
    if (!chunk) {
        return flecs::entity{};
    }

    const ChunkSlotRef slot_ref = store.slot_ref_for(coord);
    flecs::entity entity = world.entity()
                               .set<ChunkCoord>(coord)
                               .set<ChunkSlotRef>(slot_ref);

    store.set_entity_for(coord, entity.id());

    const EvtChunkLoaded loaded{coord};
    world.event<EvtChunkLoaded>().entity(entity).ctx(loaded).emit();

    SPDLOG_DEBUG(
        "mesh queue stub: schedule remesh chunk=({},{},{}) slot_id={}",
        coord.x,
        coord.y,
        coord.z,
        slot_ref.slot_id);

    return entity;
}

void unload_chunk(flecs::world& world, ChunkStore& store, ChunkCoord coord) {
    if (store.try_get(coord) == nullptr) {
        return;
    }

    store.set_pending_unload(coord, true);

    const uint64_t entity_id = store.entity_for(coord);
    if (entity_id != 0) {
        flecs::entity entity(world, entity_id);
        if (entity.is_alive()) {
            entity.destruct();
        }
    }

    store.free(coord);

    flecs::entity source = world.lookup("WorldRoot");
    if (!source) {
        source = world.entity("WorldRoot").add<WorldRoot>();
    }
    const EvtChunkUnloaded unloaded{coord};
    world.event<EvtChunkUnloaded>().id<WorldRoot>().entity(source).ctx(unloaded).emit();
}

} // namespace engine
