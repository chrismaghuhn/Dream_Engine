#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockInteraction.hpp"
#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/persist/SaveService.hpp"
#include "engine/world/BlockPos.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldModule.hpp"
#include "engine/world/WorldPosition.hpp"

#include <chrono>
#include <filesystem>

namespace {

engine::WorldPosition make_player_position() {
    return engine::WorldPosition::from_world_blocks(10, 20, 30);
}

std::filesystem::path unique_temp_save_root() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("engine_m5_save_" + std::to_string(tick));
}

} // namespace

TEST_CASE("minimal save reload preserves player-placed block") {
    const std::filesystem::path saves_root = unique_temp_save_root();

    std::error_code ec;
    std::filesystem::remove_all(saves_root, ec);
    std::filesystem::create_directories(saves_root, ec);

    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(8, 8, 8);
    const engine::BlockState air = engine::make_block_state(engine::BLOCK_AIR, 0);
    REQUIRE(store.write_block(target, air));
    REQUIRE(engine::place_block_at(world, store, target, engine::BLOCK_STONE, 1));

    const engine::Chunk* saved_chunk = store.try_get(coord);
    REQUIRE(saved_chunk != nullptr);
    REQUIRE((saved_chunk->flags & engine::CHUNK_MODIFIED_BY_PLAYER) != 0);

    engine::SaveWorldRequest request{};
    request.saves_root = saves_root;
    request.world_name = "test_world";
    request.world_config = world_config;
    request.player_position = make_player_position();
    engine::Inventory inventory{};
    inventory.seed_default_hotbar();
    request.inventory = inventory.snapshot();
    REQUIRE(engine::SaveService::save_world(request, store));

    engine::ChunkStore reloaded;
    reloaded.init(16);

    engine::WorldPosition loaded_position{};
    engine::InventorySnapshot loaded_inventory{};
    REQUIRE(engine::SaveService::load_world(request, loaded_position, loaded_inventory, reloaded));
    REQUIRE(loaded_position.chunk == request.player_position.chunk);
    REQUIRE(loaded_position.local.x == request.player_position.local.x);
    REQUIRE(loaded_position.local.y == request.player_position.local.y);
    REQUIRE(loaded_position.local.z == request.player_position.local.z);

    const engine::BlockState reloaded_state = reloaded.read_block(target);
    REQUIRE(engine::block_id(reloaded_state) == engine::BLOCK_STONE);

    std::filesystem::remove_all(saves_root, ec);
}
