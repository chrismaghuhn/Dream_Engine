#pragma once

#include "engine/gameplay/BlockMutation.hpp"
#include "engine/gameplay/Camera.hpp"
#include "engine/gameplay/CreativeBlockPicker.hpp"
#include "engine/gameplay/Inventory.hpp"
#include "engine/platform/Input.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"

#include <flecs.h>
#include <optional>

namespace engine {

struct BlockRaycastHit {
    bool hit = false;
    BlockPos block{};
    BlockPos place_pos{};
};

[[nodiscard]] std::optional<BlockRaycastHit> raycast_blocks(
    const Camera& camera,
    const ChunkStore& store,
    float max_reach);

[[nodiscard]] BlockMutationResult apply_block_mutation(
    flecs::world& world,
    ChunkStore& store,
    const BlockMutation& mutation);

[[nodiscard]] bool break_block_at(
    flecs::world& world,
    ChunkStore& store,
    BlockPos pos,
    uint64_t tick,
    uint64_t source_entity = 0);

[[nodiscard]] bool place_block_at(
    flecs::world& world,
    ChunkStore& store,
    BlockPos pos,
    BlockId block_id,
    uint64_t tick,
    uint64_t source_entity = 0);

void handle_block_input(
    flecs::world& world,
    ChunkStore& store,
    const Camera& camera,
    const Input& input,
    const WorldConfig& world_config,
    const Inventory& inventory,
    bool creative_place,
    const CreativeBlockPicker* creative_picker,
    uint64_t tick,
    uint64_t source_entity = 0);

} // namespace engine
