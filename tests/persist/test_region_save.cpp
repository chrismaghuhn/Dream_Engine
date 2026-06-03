#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockInteraction.hpp"
#include "engine/persist/AtomicFile.hpp"
#include "engine/persist/Quarantine.hpp"
#include "engine/persist/RegionFileSaveBackend.hpp"
#include "engine/persist/SaveService.hpp"
#include "engine/world/BlockPos.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldModule.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>

namespace {

engine::WorldPosition make_player_position() {
    return engine::WorldPosition::from_world_blocks(10, 20, 30);
}

std::filesystem::path unique_temp_save_root() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("engine_m8_save_" + std::to_string(tick));
}

[[nodiscard]] std::optional<std::size_t> chunk_blob_offset_in_region(
    std::span<const std::uint8_t> region_bytes,
    int slot) {
    if (region_bytes.size() < 16 + 8) {
        return std::nullopt;
    }
    const std::size_t base = 16 + static_cast<std::size_t>(slot) * 8;
    const std::uint32_t offset_sectors =
        static_cast<std::uint32_t>(region_bytes[base])
        | (static_cast<std::uint32_t>(region_bytes[base + 1]) << 8)
        | (static_cast<std::uint32_t>(region_bytes[base + 2]) << 16);
    const std::uint32_t blob_size =
        static_cast<std::uint32_t>(region_bytes[base + 3])
        | (static_cast<std::uint32_t>(region_bytes[base + 4]) << 8)
        | ((static_cast<std::uint32_t>(region_bytes[base + 5]) & 0x0Fu) << 16);
    if (offset_sectors == 0 || blob_size == 0) {
        return std::nullopt;
    }
    const std::size_t offset =
        static_cast<std::size_t>(offset_sectors) * engine::kVwrSectorSize;
    if (offset + blob_size > region_bytes.size()) {
        return std::nullopt;
    }
    return offset;
}

engine::SaveWorldRequest make_request(const std::filesystem::path& saves_root) {
    engine::SaveWorldRequest request{};
    request.saves_root = saves_root;
    request.world_name = "region_world";
    request.world_config = engine::WorldConfig{};
    request.player_position = make_player_position();
    engine::Inventory inventory{};
    inventory.seed_default_hotbar();
    request.inventory = inventory.snapshot();
    return request;
}

void place_stone_at_origin(engine::ChunkStore& store, flecs::world& world) {
    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());
    const engine::BlockPos target = engine::BlockPos::from_world_blocks(8, 8, 8);
    const engine::BlockState air = engine::make_block_state(engine::BLOCK_AIR, 0);
    REQUIRE(store.write_block(target, air));
    REQUIRE(engine::place_block_at(world, store, target, engine::BLOCK_STONE, 1));
}

} // namespace

TEST_CASE("region save roundtrip preserves one modified chunk") {
    const std::filesystem::path saves_root = unique_temp_save_root();
    std::error_code ec;
    std::filesystem::remove_all(saves_root, ec);
    std::filesystem::create_directories(saves_root, ec);

    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);
    place_stone_at_origin(store, world);

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(8, 8, 8);
    REQUIRE(engine::block_id(store.read_block(target)) == engine::BLOCK_STONE);

    const engine::SaveWorldRequest request = make_request(saves_root);
    REQUIRE(engine::SaveService::save_world(request, store));

    engine::ChunkStore reloaded;
    reloaded.init(16);
    engine::WorldPosition loaded_position{};
    engine::InventorySnapshot loaded_inventory{};
    REQUIRE(engine::SaveService::load_world(request, loaded_position, loaded_inventory, reloaded));

    REQUIRE(engine::block_id(reloaded.read_block(target)) == engine::BLOCK_STONE);

    std::filesystem::remove_all(saves_root, ec);
}

TEST_CASE("region load crc failure quarantines player-modified chunk") {
    const std::filesystem::path saves_root = unique_temp_save_root();
    std::error_code ec;
    std::filesystem::remove_all(saves_root, ec);
    std::filesystem::create_directories(saves_root, ec);

    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);
    place_stone_at_origin(store, world);

    const engine::SaveWorldRequest request = make_request(saves_root);
    REQUIRE(engine::SaveService::save_world(request, store));

    const std::filesystem::path region_path =
        engine::SaveService::world_dir(request) / "regions" / "r.0.0.0.vwr";
    std::vector<std::uint8_t> bytes;
    REQUIRE(engine::read_file_bytes(region_path, bytes));

    const std::optional<std::size_t> blob_offset = chunk_blob_offset_in_region(
        bytes,
        engine::local_chunk_index(engine::ChunkCoord{0, 0, 0}));
    REQUIRE(blob_offset.has_value());
    bytes[*blob_offset + 8] ^= 0xFFu;
    REQUIRE(engine::atomic_write_file(region_path, bytes));

    engine::ChunkStore reloaded;
    reloaded.init(16);
    engine::WorldPosition loaded_position{};
    engine::InventorySnapshot loaded_inventory{};
    REQUIRE(engine::SaveService::load_world(request, loaded_position, loaded_inventory, reloaded));

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(8, 8, 8);
    REQUIRE(engine::block_id(reloaded.read_block(target)) != engine::BLOCK_STONE);
    REQUIRE(std::filesystem::exists(engine::quarantine_chunk_path(
        engine::SaveService::world_dir(request),
        engine::ChunkCoord{0, 0, 0})));

    std::filesystem::remove_all(saves_root, ec);
}

TEST_CASE("region load version mismatch quarantines player chunk only") {
    const std::filesystem::path saves_root = unique_temp_save_root();
    std::error_code ec;
    std::filesystem::remove_all(saves_root, ec);
    std::filesystem::create_directories(saves_root, ec);

    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);
    place_stone_at_origin(store, world);

    const engine::SaveWorldRequest request = make_request(saves_root);
    REQUIRE(engine::SaveService::save_world(request, store));

    const std::filesystem::path region_path =
        engine::SaveService::world_dir(request) / "regions" / "r.0.0.0.vwr";
    std::vector<std::uint8_t> bytes;
    REQUIRE(engine::read_file_bytes(region_path, bytes));

    const std::optional<std::size_t> blob_offset = chunk_blob_offset_in_region(
        bytes,
        engine::local_chunk_index(engine::ChunkCoord{0, 0, 0}));
    REQUIRE(blob_offset.has_value());
    const std::uint16_t bad_version = 99;
    std::memcpy(bytes.data() + *blob_offset + 12, &bad_version, sizeof(bad_version));
    REQUIRE(engine::atomic_write_file(region_path, bytes));

    engine::ChunkStore reloaded;
    reloaded.init(16);
    engine::WorldPosition loaded_position{};
    engine::InventorySnapshot loaded_inventory{};
    REQUIRE(engine::SaveService::load_world(request, loaded_position, loaded_inventory, reloaded));
    REQUIRE(std::filesystem::exists(engine::quarantine_chunk_path(
        engine::SaveService::world_dir(request),
        engine::ChunkCoord{0, 0, 0})));

    std::filesystem::remove_all(saves_root, ec);
}
