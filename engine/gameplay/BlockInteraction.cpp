#include "engine/gameplay/BlockInteraction.hpp"

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/world/WorldEvents.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {

namespace {

constexpr float kRaycastEpsilon = 1e-4f;

[[nodiscard]] bool is_raycast_target(const ChunkStore& store, const BlockPos& pos) {
    const BlockState state = store.read_block(pos);
    return is_solid(block_id(state));
}

void mark_chunk_dirty(flecs::world& world, ChunkStore& store, ChunkCoord coord) {
    const uint64_t entity_id = store.entity_for(coord);
    if (entity_id == 0) {
        return;
    }

    flecs::entity chunk_entity(world, entity_id);
    if (!chunk_entity.is_alive()) {
        return;
    }

    if (!chunk_entity.has<ChunkDirty>()) {
        chunk_entity.add<ChunkDirty>();
    }
}

void emit_block_broken(flecs::world& world, ChunkStore& store, const BlockMutation& mutation) {
    const uint64_t entity_id = store.entity_for(mutation.pos.chunk);
    if (entity_id == 0) {
        return;
    }

    flecs::entity chunk_entity(world, entity_id);
    if (!chunk_entity.is_alive()) {
        return;
    }

    const EvtBlockBroken evt{
        mutation.pos.chunk,
        mutation.pos.block_local,
        mutation.old_state,
    };
    world.event<EvtBlockBroken>().id<ChunkCoord>().entity(chunk_entity).ctx(evt).emit();
}

void emit_block_placed(flecs::world& world, ChunkStore& store, const BlockMutation& mutation) {
    const uint64_t entity_id = store.entity_for(mutation.pos.chunk);
    if (entity_id == 0) {
        return;
    }

    flecs::entity chunk_entity(world, entity_id);
    if (!chunk_entity.is_alive()) {
        return;
    }

    const EvtBlockPlaced evt{
        mutation.pos.chunk,
        mutation.pos.block_local,
        mutation.new_state,
    };
    world.event<EvtBlockPlaced>().id<ChunkCoord>().entity(chunk_entity).ctx(evt).emit();
}

} // namespace

std::optional<BlockRaycastHit> raycast_blocks(
    const Camera& camera,
    const ChunkStore& store,
    float max_reach) {
    if (max_reach <= 0.f) {
        return std::nullopt;
    }

    const glm::vec3 origin = camera.position;
    const glm::vec3 direction = glm::normalize(camera.forward());
    const glm::vec3 end = origin + direction * max_reach;

    glm::ivec3 voxel = glm::ivec3(glm::floor(origin + direction * kRaycastEpsilon));
    const glm::ivec3 end_voxel = glm::ivec3(glm::floor(end));

    const glm::ivec3 step{
        direction.x < 0.f ? -1 : (direction.x > 0.f ? 1 : 0),
        direction.y < 0.f ? -1 : (direction.y > 0.f ? 1 : 0),
        direction.z < 0.f ? -1 : (direction.z > 0.f ? 1 : 0),
    };

    glm::vec3 t_max{
        step.x != 0
            ? ((static_cast<float>(voxel.x) + (step.x > 0 ? 1.f : 0.f)) - origin.x) / direction.x
            : std::numeric_limits<float>::infinity(),
        step.y != 0
            ? ((static_cast<float>(voxel.y) + (step.y > 0 ? 1.f : 0.f)) - origin.y) / direction.y
            : std::numeric_limits<float>::infinity(),
        step.z != 0
            ? ((static_cast<float>(voxel.z) + (step.z > 0 ? 1.f : 0.f)) - origin.z) / direction.z
            : std::numeric_limits<float>::infinity(),
    };

    const glm::vec3 t_delta{
        step.x != 0 ? static_cast<float>(step.x) / direction.x : std::numeric_limits<float>::infinity(),
        step.y != 0 ? static_cast<float>(step.y) / direction.y : std::numeric_limits<float>::infinity(),
        step.z != 0 ? static_cast<float>(step.z) / direction.z : std::numeric_limits<float>::infinity(),
    };

    std::optional<BlockPos> previous_cell;
    const int max_steps = static_cast<int>(std::ceil(max_reach)) + 2;

    for (int i = 0; i < max_steps; ++i) {
        const BlockPos current = BlockPos::from_world_blocks(voxel.x, voxel.y, voxel.z);
        if (is_raycast_target(store, current)) {
            BlockRaycastHit hit{};
            hit.hit = true;
            hit.block = current;
            if (previous_cell) {
                hit.place_pos = *previous_cell;
            } else {
                hit.place_pos = current;
            }
            return hit;
        }

        previous_cell = current;

        if (voxel == end_voxel) {
            break;
        }

        if (t_max.x < t_max.y) {
            if (t_max.x < t_max.z) {
                voxel.x += step.x;
                t_max.x += std::abs(t_delta.x);
            } else {
                voxel.z += step.z;
                t_max.z += std::abs(t_delta.z);
            }
        } else if (t_max.y < t_max.z) {
            voxel.y += step.y;
            t_max.y += std::abs(t_delta.y);
        } else {
            voxel.z += step.z;
            t_max.z += std::abs(t_delta.z);
        }
    }

    return std::nullopt;
}

BlockMutationResult apply_block_mutation(
    flecs::world& world,
    ChunkStore& store,
    const BlockMutation& mutation) {
    BlockMutationResult result{};

    if (store.read_block(mutation.pos).raw != mutation.old_state.raw) {
        return result;
    }

    if (!store.write_block(mutation.pos, mutation.new_state)) {
        return result;
    }

    mark_chunk_dirty(world, store, mutation.pos.chunk);

    const bool breaking =
        is_solid(block_id(mutation.old_state)) && block_id(mutation.new_state) == BLOCK_AIR;
    const bool placing =
        block_id(mutation.old_state) == BLOCK_AIR && is_solid(block_id(mutation.new_state));

    if (breaking) {
        emit_block_broken(world, store, mutation);
    } else if (placing) {
        emit_block_placed(world, store, mutation);
    }

    result.applied = true;
    return result;
}

bool break_block_at(
    flecs::world& world,
    ChunkStore& store,
    BlockPos pos,
    uint64_t tick,
    uint64_t source_entity) {
    const BlockState old_state = store.read_block(pos);
    if (!is_solid(block_id(old_state))) {
        return false;
    }

    const BlockMutation mutation{
        pos,
        old_state,
        make_block_state(BLOCK_AIR, 0),
        source_entity,
        tick,
    };
    return apply_block_mutation(world, store, mutation).applied;
}

bool place_block_at(
    flecs::world& world,
    ChunkStore& store,
    BlockPos pos,
    BlockId block_id_to_place,
    uint64_t tick,
    uint64_t source_entity) {
    if (!is_solid(block_id_to_place)) {
        return false;
    }

    const BlockState old_state = store.read_block(pos);
    if (block_id(old_state) != BLOCK_AIR) {
        return false;
    }

    const BlockMutation mutation{
        pos,
        old_state,
        make_block_state(block_id_to_place, 0),
        source_entity,
        tick,
    };
    return apply_block_mutation(world, store, mutation).applied;
}

void handle_block_input(
    flecs::world& world,
    ChunkStore& store,
    const Camera& camera,
    const Input& input,
    const WorldConfig& world_config,
    CreativeBlockPicker& picker,
    uint64_t tick,
    uint64_t source_entity) {
    const auto hit = raycast_blocks(camera, store, world_config.player_reach);
    if (!hit || !hit->hit) {
        return;
    }

    if (input.break_pressed()) {
        (void)break_block_at(world, store, hit->block, tick, source_entity);
        return;
    }

    if (input.place_pressed()) {
        (void)place_block_at(
            world, store, hit->place_pos, picker.selected_id(), tick, source_entity);
    }
}

} // namespace engine
