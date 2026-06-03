#include "engine/world/WorldModule.hpp"

#include "engine/world/WorldEvents.hpp"

#include <spdlog/spdlog.h>

namespace engine {

WorldModule::WorldModule(flecs::world& ecs) {
    ecs.module<WorldModule>();

    ECS_EVENT(ecs, EvtOriginShift);
    ECS_EVENT(ecs, EvtChunkLoaded);
    ECS_EVENT(ecs, EvtChunkUnloaded);
    ECS_EVENT(ecs, EvtChunkDirty);
    ECS_EVENT(ecs, EvtChunkMeshReady);
    ECS_EVENT(ecs, EvtBlockBroken);
    ECS_EVENT(ecs, EvtBlockPlaced);
    ecs.component<ChunkDirty>();
    ecs.component<WorldRoot>();
    ecs.entity("WorldRoot").add<WorldRoot>();

    ecs.observer<ChunkDirty>()
        .event(flecs::OnAdd)
        .each([](flecs::entity e, ChunkDirty) {
            SPDLOG_DEBUG("ChunkDirty added entity={}", e.id());
        });

    ecs.observer<EvtBlockBroken>()
        .event<EvtBlockBroken>()
        .each([](flecs::iter&, size_t, EvtBlockBroken&) {});

    ecs.observer<EvtBlockPlaced>()
        .event<EvtBlockPlaced>()
        .each([](flecs::iter&, size_t, EvtBlockPlaced&) {});
}

} // namespace engine
