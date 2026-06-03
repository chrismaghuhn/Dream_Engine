#pragma once

#include "engine/gameplay/Inventory.hpp"
#include "engine/world/WorldPosition.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace engine {

constexpr char kPlayerDatMagic[4] = {'V', 'P', 'L', '1'};
constexpr std::uint16_t kPlayerDatVersion = 1;
constexpr std::uint16_t kPlayerDatHotbarPersistSlots = static_cast<std::uint16_t>(kHotbarSlots);

struct PlayerSaveV1 {
    WorldPosition position{};
    float health = 20.f;
    float hunger = 0.f;
    InventorySnapshot inventory{};
};

[[nodiscard]] std::vector<std::uint8_t> encode_player_dat_v1(const PlayerSaveV1& player);
[[nodiscard]] bool decode_player_dat_v1(std::span<const std::uint8_t> bytes, PlayerSaveV1& out);

[[nodiscard]] bool write_player_dat_atomic(const std::filesystem::path& path, const PlayerSaveV1& player);
[[nodiscard]] bool read_player_dat(const std::filesystem::path& path, PlayerSaveV1& out);

} // namespace engine
