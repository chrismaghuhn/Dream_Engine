#include "engine/world/ChunkLifecycle.hpp"

#include "engine/procgen/TerrainGraph.hpp"
#include "engine/render/GpuDeferredFreeQueue.hpp"
#include "engine/world/BlockLight.hpp"

#include <array>

#include <spdlog/spdlog.h>

namespace engine {

namespace {

ChunkGpuServices* g_gpu_services = nullptr;

void enqueue_chunk_mesh_slots(flecs::entity entity) {
    if (g_gpu_services == nullptr || g_gpu_services->deferred_free == nullptr) {
        return;
    }

    const ChunkMeshSlots* slots = entity.get<ChunkMeshSlots>();
    if (slots == nullptr) {
        return;
    }

    const std::uint32_t snapshot_slot =
        g_gpu_services->submit_snapshot_slot ? g_gpu_services->submit_snapshot_slot() : 0;
    for (const std::uint32_t slot_id : slots->section_slot_ids) {
        if (slot_id != 0) {
            g_gpu_services->deferred_free->enqueue_free(slot_id, snapshot_slot);
        }
    }
}

} // namespace

void set_chunk_gpu_services(ChunkGpuServices* services) {
    g_gpu_services = services;
}

void refresh_chunk_section_borders(ChunkStore& store, ChunkCoord coord) {
    for (int sx = 0; sx < 2; ++sx) {
        for (int sy = 0; sy < 2; ++sy) {
            for (int sz = 0; sz < 2; ++sz) {
                refresh_section_border_cache(store, coord, glm::ivec3{sx, sy, sz});
            }
        }
    }
}

void refresh_loaded_chunk_neighbors(ChunkStore& store, ChunkCoord coord) {
    static constexpr std::array<ChunkCoord, 6> kNeighborOffsets{
        ChunkCoord{1, 0, 0},
        ChunkCoord{-1, 0, 0},
        ChunkCoord{0, 1, 0},
        ChunkCoord{0, -1, 0},
        ChunkCoord{0, 0, 1},
        ChunkCoord{0, 0, -1},
    };

    for (const ChunkCoord& offset : kNeighborOffsets) {
        const ChunkCoord neighbor = coord + offset;
        if (store.try_get(neighbor) != nullptr && !store.is_pending_unload(neighbor)) {
            refresh_chunk_section_borders(store, neighbor);
        }
    }
}

void register_chunk_lifecycle(flecs::world& world) {
    world.component<ChunkCoord>();
    world.component<ChunkSlotRef>();

    world.observer<ChunkSlotRef>()
        .event(flecs::OnRemove)
        .each([](flecs::entity entity, ChunkSlotRef slot_ref) {
            const ChunkCoord* coord = entity.get<ChunkCoord>();
            if (coord != nullptr) {
                SPDLOG_DEBUG(
                    "on_chunk_unload coord=({},{},{}) slot_id={} generation={}",
                    coord->x,
                    coord->y,
                    coord->z,
                    slot_ref.slot_id,
                    slot_ref.generation);
            }
            enqueue_chunk_mesh_slots(entity);
        });

    world.observer<ChunkDirty>()
        .event(flecs::OnAdd)
        .each([](flecs::entity entity, ChunkDirty) {
            const ChunkCoord* coord = entity.get<ChunkCoord>();
            if (coord == nullptr) {
                SPDLOG_DEBUG("ChunkDirty entity={}", entity.id());
                return;
            }
            SPDLOG_DEBUG(
                "ChunkDirty chunk=({},{},{}) entity={}",
                coord->x,
                coord->y,
                coord->z,
                entity.id());
        });
}

flecs::entity load_chunk(
    flecs::world& world, ChunkStore& store, ChunkCoord coord, const WorldConfig& world_config) {
    const uint64_t existing = store.entity_for(coord);
    if (existing != 0 && store.try_get(coord) != nullptr) {
        return world.entity(existing);
    }

    Chunk* chunk = store.allocate(coord);
    if (!chunk) {
        return flecs::entity{};
    }

    TerrainGraph terrain(world_config.world_seed, world_config.sea_level);
    terrain.fill_chunk(*chunk);
    refresh_chunk_section_borders(store, coord);
    refresh_loaded_chunk_neighbors(store, coord);

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
