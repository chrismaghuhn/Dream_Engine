#include <catch2/catch_test_macros.hpp>

#include "engine/character/core/GltfIngest.hpp"

#include <filesystem>
#include <string>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

static const std::string kPlayerDir =
    std::string(ENGINE_SOURCE_DIR) +
    "/assets/Meshy_AI_Voxel_Dungeon_Explore_biped/";

static const std::string kPlayerBase =
    kPlayerDir + "Meshy_AI_Voxel_Dungeon_Explore_biped_Character_output.glb";

static const std::string kHighKick =
    kPlayerDir + "Meshy_AI_Voxel_Dungeon_Explore_biped_Animation_High_Kick_withSkin.glb";

TEST_CASE("loads dungeon explorer base mesh with skin weights", "[gltf_ingest]") {
    REQUIRE(std::filesystem::exists(kPlayerBase));
    engine::character::CharacterAsset asset = engine::character::GltfIngest::load_base(kPlayerBase);

    REQUIRE(asset.mesh.positions.size() > 0);
    REQUIRE(asset.mesh.joint_indices.size() == asset.mesh.positions.size());
    REQUIRE(asset.mesh.joint_weights.size() == asset.mesh.positions.size());
    REQUIRE(asset.mesh.bones.size() > 0);
    REQUIRE(asset.mesh.bones.size() <= 128);
    REQUIRE(asset.mesh.indices.size() > 0);
    REQUIRE(asset.mesh.inverse_bind_matrices.size() == asset.mesh.bones.size());
}

TEST_CASE("loads high kick animation clip", "[gltf_ingest]") {
    REQUIRE(std::filesystem::exists(kPlayerBase));
    REQUIRE(std::filesystem::exists(kHighKick));

    engine::character::CharacterAsset asset = engine::character::GltfIngest::load_base(kPlayerBase);
    REQUIRE_NOTHROW(engine::character::GltfIngest::load_animation_clip(asset, kHighKick, "High_Kick"));

    REQUIRE(asset.clips.size() == 1);
    REQUIRE(asset.clips[0].name == "High_Kick");
    REQUIRE(asset.clips[0].duration_seconds > 0.f);
    REQUIRE(asset.clips[0].channels.size() > 0);
}
