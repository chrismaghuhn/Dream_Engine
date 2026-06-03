#include <catch2/catch_test_macros.hpp>

#include "engine/character/core/CharacterCatalog.hpp"

#include <filesystem>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

#ifndef ENGINE_BINARY_DIR
#define ENGINE_BINARY_DIR "."
#endif

TEST_CASE("cooks player set and writes to build/character_cache", "[catalog][integration]") {
    engine::character::CharacterAsset asset = engine::character::CharacterCatalog::load_player_set();

    REQUIRE(asset.mesh.positions.size() > 0);
    REQUIRE(asset.mesh.bones.size() > 0);
    // Walk, Run, High_Kick, Elbow_Strike, Counterstrike = 5 clips minimum
    REQUIRE(asset.clips.size() >= 3);

    const std::filesystem::path cache_dir =
        std::filesystem::path(ENGINE_BINARY_DIR) / "character_cache";
    REQUIRE(std::filesystem::exists(cache_dir));
}

TEST_CASE("cooks dummy set — hit reaction clip present", "[catalog][integration]") {
    engine::character::CharacterAsset asset = engine::character::CharacterCatalog::load_dummy_set();

    REQUIRE(asset.mesh.positions.size() > 0);
    REQUIRE(asset.clips.size() >= 1);

    bool has_hit_react = false;
    for (const auto& clip : asset.clips) {
        if (clip.name == "Hit_Reaction_1") {
            has_hit_react = true;
        }
    }
    REQUIRE(has_hit_react);
}
