#include <catch2/catch_test_macros.hpp>

#include "engine/character/core/GltfIngest.hpp"
#include "engine/character/core/SkeletonValidator.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

static const std::string kPlayerBase =
    std::string(ENGINE_SOURCE_DIR) +
    "/assets/Meshy_AI_Voxel_Dungeon_Explore_biped/"
    "Meshy_AI_Voxel_Dungeon_Explore_biped_Character_output.glb";

static const std::string kHighKick =
    std::string(ENGINE_SOURCE_DIR) +
    "/assets/Meshy_AI_Voxel_Dungeon_Explore_biped/"
    "Meshy_AI_Voxel_Dungeon_Explore_biped_Animation_High_Kick_withSkin.glb";

TEST_CASE("validator accepts identical skeleton", "[skeleton_compat]") {
    using namespace engine::character;
    std::vector<BoneInfo> bones = {{"root", -1}, {"hip", 0}, {"spine", 1}};
    const auto result = SkeletonValidator::validate_against_base(bones, bones);
    REQUIRE(result.ok);
}

TEST_CASE("validator rejects joint count mismatch", "[skeleton_compat]") {
    using namespace engine::character;
    std::vector<BoneInfo> base = {{"root", -1}, {"hip", 0}};
    std::vector<BoneInfo> anim = {{"root", -1}};
    const auto result = SkeletonValidator::validate_against_base(base, anim);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("count") != std::string::npos);
}

TEST_CASE("validator rejects unknown joint name", "[skeleton_compat]") {
    using namespace engine::character;
    std::vector<BoneInfo> base = {{"root", -1}, {"hip", 0}};
    std::vector<BoneInfo> anim = {{"root", -1}, {"UNKNOWN", 0}};
    const auto result = SkeletonValidator::validate_against_base(base, anim);
    REQUIRE_FALSE(result.ok);
}

TEST_CASE("validator rejects parent hierarchy mismatch", "[skeleton_compat]") {
    using namespace engine::character;
    std::vector<BoneInfo> base = {{"root", -1}, {"hip", 0}, {"spine", 0}};
    std::vector<BoneInfo> anim = {{"root", -1}, {"hip", 0}, {"spine", 1}}; // spine parent differs
    const auto result = SkeletonValidator::validate_against_base(base, anim);
    REQUIRE_FALSE(result.ok);
}

TEST_CASE("accepts matching meshy animation skeleton", "[skeleton_compat][integration]") {
    if (!std::filesystem::exists(kPlayerBase) || !std::filesystem::exists(kHighKick)) {
        SKIP("Meshy assets not present");
    }
    engine::character::CharacterAsset asset =
        engine::character::GltfIngest::load_base(kPlayerBase);
    REQUIRE_NOTHROW(
        engine::character::GltfIngest::load_animation_clip(asset, kHighKick, "High_Kick"));
}
