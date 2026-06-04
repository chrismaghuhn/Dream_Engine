#include <catch2/catch_test_macros.hpp>

#include "engine/character/core/CharacterCatalog.hpp"

#include <filesystem>
#include <string>
#include <vector>

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
    const auto has_clip = [&](const std::string& name) {
        for (const auto& clip : asset.clips) {
            if (clip.name == name) return true;
        }
        return false;
    };

    // Walk, Run, the eleven Fight combat clips, plus the Idle rest pose.
    REQUIRE(asset.clips.size() >= 14);

    const std::vector<std::string> expected_clips = {
        "Walk", "Run", "Idle",
        "Jab_Left", "Jab_Right", "Hook_Left", "Uppercut_Right",
        "Uppercut_Left", "Upper_Hook_Right", "Knee_Strike",
        "Kick_High_Step", "Roundhouse", "Spin_Kick", "Shield_Push",
    };
    for (const std::string& expected : expected_clips) {
        REQUIRE(has_clip(expected));
    }

    // The replaced over-long clips must NOT be loaded anymore.
    REQUIRE_FALSE(has_clip("Counterstrike"));
    REQUIRE_FALSE(has_clip("Dodge_and_Counter"));
    REQUIRE_FALSE(has_clip("Elbow_Strike"));

    const std::filesystem::path cache_dir =
        std::filesystem::path(ENGINE_BINARY_DIR) / "character_cache";
    REQUIRE(std::filesystem::exists(cache_dir));
}

TEST_CASE("cooks dummy set hit reaction clip present", "[catalog][integration]") {
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
