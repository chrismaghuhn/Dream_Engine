#pragma once

#include "engine/core/JobSystem.hpp"
#include "engine/gameplay/Inventory.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldPosition.hpp"

#include <filesystem>
#include <string>

namespace engine {

using RegionCoord = glm::ivec3;

constexpr char kVwrMagic[4] = {'V', 'W', 'R', '1'};
constexpr std::uint16_t kVwrFileVersion = 1;
constexpr std::uint8_t kVwrCompressionZstd = 1;
constexpr std::uint8_t kVwrSectorShift = 12;
constexpr std::size_t kVwrSectorSize = 1u << kVwrSectorShift;
constexpr std::size_t kVwrDataStartSector = 2;
constexpr std::uint16_t kCurrentChunkBlobVersion = 1;

[[nodiscard]] RegionCoord region_of(ChunkCoord chunk);
[[nodiscard]] int local_chunk_index(ChunkCoord chunk);

[[nodiscard]] std::filesystem::path save_world_dir(
    const std::filesystem::path& saves_root,
    const std::string& world_name);

[[nodiscard]] bool migrate_minimal_to_region(
    const std::filesystem::path& world_dir,
    JobSystem* jobs);

[[nodiscard]] bool region_save_world(
    const std::filesystem::path& world_dir,
    const WorldConfig& world_config,
    const WorldPosition& player_position,
    const InventorySnapshot& inventory,
    ChunkStore& store,
    JobSystem* jobs);

[[nodiscard]] bool region_load_world(
    const std::filesystem::path& world_dir,
    const WorldConfig& world_config,
    WorldPosition& out_player_position,
    InventorySnapshot& out_inventory,
    ChunkStore& store);

} // namespace engine
