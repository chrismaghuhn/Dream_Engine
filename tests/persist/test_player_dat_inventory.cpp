#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/Inventory.hpp"
#include "engine/persist/PlayerDat.hpp"

#include <cstring>
#include <vector>

TEST_CASE("player.dat v1 round-trips hotbar inventory snapshot") {
    engine::PlayerSaveV1 player{};
    player.inventory.hotbar[0] = engine::ItemStack{static_cast<engine::ItemId>(engine::BLOCK_STONE), 42};
    player.inventory.hotbar[1] = engine::ItemStack{static_cast<engine::ItemId>(engine::BLOCK_DIRT), 7};
    player.inventory.hotbar[2] = engine::ItemStack{static_cast<engine::ItemId>(engine::BLOCK_TORCH), 1};
    player.inventory.hotbar_selected = 2;

    const std::vector<std::uint8_t> encoded = engine::encode_player_dat_v1(player);
    engine::PlayerSaveV1 decoded{};
    REQUIRE(engine::decode_player_dat_v1(encoded, decoded));

    REQUIRE(decoded.inventory.hotbar_selected == 2);
    REQUIRE(decoded.inventory.hotbar[0].item_id == engine::BLOCK_STONE);
    REQUIRE(decoded.inventory.hotbar[0].count == 42);
    REQUIRE(decoded.inventory.hotbar[1].item_id == engine::BLOCK_DIRT);
    REQUIRE(decoded.inventory.hotbar[1].count == 7);
    REQUIRE(decoded.inventory.hotbar[2].item_id == engine::BLOCK_TORCH);
    REQUIRE(decoded.inventory.hotbar[2].count == 1);
}

TEST_CASE("player.dat v1 decodes legacy zero-slot inventory tail") {
    engine::PlayerSaveV1 player{};
    player.health = 20.f;
    player.inventory.hotbar_selected = 4;

    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), std::begin(engine::kPlayerDatMagic), std::end(engine::kPlayerDatMagic));
    auto append_u16 = [&](std::uint16_t value) {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    };
    auto append_i32 = [&](std::int32_t value) {
        const auto raw = static_cast<std::uint32_t>(value);
        for (int i = 0; i < 4; ++i) {
            bytes.push_back(static_cast<std::uint8_t>((raw >> (8 * i)) & 0xFFu));
        }
    };
    auto append_f32 = [&](float value) {
        std::uint32_t raw = 0;
        std::memcpy(&raw, &value, sizeof(raw));
        for (int i = 0; i < 4; ++i) {
            bytes.push_back(static_cast<std::uint8_t>((raw >> (8 * i)) & 0xFFu));
        }
    };

    append_u16(engine::kPlayerDatVersion);
    append_i32(0);
    append_i32(0);
    append_i32(0);
    append_f32(0.f);
    append_f32(0.f);
    append_f32(0.f);
    append_f32(player.health);
    append_f32(player.hunger);
    bytes.push_back(player.inventory.hotbar_selected);
    bytes.push_back(0);
    bytes.push_back(0);
    bytes.push_back(0);
    append_u16(0);

    engine::PlayerSaveV1 decoded{};
    REQUIRE(engine::decode_player_dat_v1(bytes, decoded));
    REQUIRE(decoded.inventory.hotbar_selected == 4);
}

TEST_CASE("inventory selected block id follows hotbar slot") {
    engine::Inventory inventory{};
    inventory.seed_default_hotbar();
    inventory.set_hotbar_selected(1);
    REQUIRE(inventory.selected_block_id() == engine::BLOCK_DIRT);
}
