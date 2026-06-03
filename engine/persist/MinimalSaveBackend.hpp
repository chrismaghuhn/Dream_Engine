#pragma once

#include "engine/persist/PlayerDat.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldPosition.hpp"

#include <filesystem>
#include <string>

namespace engine {

struct MinimalSaveWorldMeta {
    std::uint64_t world_seed = 0;
    std::string format = "minimal_m5";
    std::uint32_t version = 1;
};

[[nodiscard]] std::filesystem::path minimal_save_world_dir(
    const std::filesystem::path& saves_root,
    const std::string& world_name);

[[nodiscard]] bool minimal_save_world(
    const std::filesystem::path& world_dir,
    const WorldConfig& world_config,
    const WorldPosition& player_position,
    ChunkStore& store);

[[nodiscard]] bool minimal_load_world(
    const std::filesystem::path& world_dir,
    const WorldConfig& world_config,
    WorldPosition& out_player_position,
    ChunkStore& store);

} // namespace engine
