#include "engine/world/WorldModule.hpp"

#include "engine/world/ChunkLifecycle.hpp"
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
    ecs.component<ChunkCoord>();
    ecs.component<ChunkSlotRef>();
    ecs.component<ChunkDirty>();
    ecs.component<ChunkMeshSlots>();
    ecs.component<WorldRoot>();
    ecs.entity("WorldRoot").add<WorldRoot>();

    register_chunk_lifecycle(ecs);

    ecs.observer()
        .event<EvtBlockBroken>()
        .with<ChunkCoord>()
        .run([](flecs::iter&) {});

    ecs.observer()
        .event<EvtBlockPlaced>()
        .with<ChunkCoord>()
        .run([](flecs::iter&) {});
}

} // namespace engine
