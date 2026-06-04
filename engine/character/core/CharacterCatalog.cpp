#include "engine/character/core/CharacterCatalog.hpp"

#include "engine/character/core/CookedCharacterCache.hpp"
#include "engine/character/core/GltfIngest.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

namespace engine::character {

namespace {

const std::string kPlayerDir =
    std::string(ENGINE_SOURCE_DIR) +
    "/assets/Meshy_AI_Voxel_Dungeon_Explore_biped/";

const std::string kDummyDir =
    std::string(ENGINE_SOURCE_DIR) +
    "/assets/Meshy_AI_Voxel_Straw_Fantasy_D_biped/";

std::string player_glb(const std::string& suffix) {
    return kPlayerDir + "Meshy_AI_Voxel_Dungeon_Explore_biped_" + suffix;
}

std::string dummy_glb(const std::string& suffix) {
    return kDummyDir + "Meshy_AI_Voxel_Straw_Fantasy_D_biped_" + suffix;
}

void require_exists(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("CharacterCatalog: asset not found: " + path);
    }
}

} // namespace

CharacterAsset CharacterCatalog::load_player_set() {
    const std::string base_path = player_glb("Character_output.glb");
    require_exists(base_path);

    CharacterAsset asset = CookedCharacterCache::load_or_cook(base_path, [&] {
        return GltfIngest::load_base(base_path);
    });

    auto append_clip = [&](const std::string& anim_path, const std::string& clip_name) {
        require_exists(anim_path);
        const std::string cache_key = base_path + "|" + anim_path + "|" + clip_name;
        CharacterAsset tmp = CookedCharacterCache::load_or_cook(cache_key, [&] {
            CharacterAsset t = GltfIngest::load_base(base_path);
            GltfIngest::load_animation_clip(t, anim_path, clip_name);
            return t;
        });
        if (!tmp.clips.empty()) {
            asset.clips.push_back(std::move(tmp.clips.back()));
        }
    };

    // Locomotion from the player directory.
    append_clip(player_glb("Animation_Walking_withSkin.glb"), "Walk");
    append_clip(player_glb("Animation_Running_withSkin.glb"), "Run");

    // Combat clips from assets/Fight (verified skeleton-compatible with the rig).
    const std::string fight_dir = std::string(ENGINE_SOURCE_DIR) + "/assets/Fight/";
    const std::pair<std::string, std::string> combat[] = {
        {"Meshy_AI_Animation_Left_Jab_from_Guard_withSkin.glb",          "Jab_Left"},
        {"Meshy_AI_Animation_Right_Jab_from_Guard_withSkin.glb",         "Jab_Right"},
        {"Meshy_AI_Animation_Left_Hook_from_Guard_withSkin.glb",         "Hook_Left"},
        {"Meshy_AI_Animation_Right_Uppercut_from_Guard_withSkin.glb",    "Uppercut_Right"},
        {"Meshy_AI_Animation_Left_Uppercut_from_Guard_withSkin.glb",     "Uppercut_Left"},
        {"Meshy_AI_Animation_Right_Upper_Hook_from_Guard_withSkin.glb",  "Upper_Hook_Right"},
        {"Meshy_AI_Animation_Boxing_Guard_Step_Knee_Strike_withSkin.glb","Knee_Strike"},
        {"Meshy_AI_Animation_Step_in_High_Kick_withSkin.glb",            "Kick_High_Step"},
        {"Meshy_AI_Animation_Roundhouse_Kick_withSkin.glb",              "Roundhouse"},
        {"Meshy_AI_Animation_Lunge_Spin_Kick_withSkin.glb",              "Spin_Kick"},
        {"Meshy_AI_Animation_Shield_Push_Left_withSkin.glb",             "Shield_Push"},
        {"Meshy_AI_Animation_Idle_7_withSkin.glb",                       "Idle"},
    };
    for (const auto& [file, clip_name] : combat) {
        append_clip(fight_dir + file, clip_name);
    }

    SPDLOG_INFO("CharacterCatalog: player set loaded — {} clips", asset.clips.size());
    return asset;
}

CharacterAsset CharacterCatalog::load_dummy_set() {
    // The Straw Fantasy biped has no separate Character_output.glb;
    // use the Hit_Reaction_1 GLB as both base mesh and the default hit clip.
    const std::string hit_react_path =
        dummy_glb("Animation_Hit_Reaction_1_withSkin.glb");
    require_exists(hit_react_path);

    CharacterAsset asset = CookedCharacterCache::load_or_cook(hit_react_path, [&] {
        CharacterAsset tmp = GltfIngest::load_base(hit_react_path);
        // The first animation in the file IS the hit reaction.
        GltfIngest::load_animation_clip(tmp, hit_react_path, "Hit_Reaction_1");
        return tmp;
    });

    SPDLOG_INFO("CharacterCatalog: dummy set loaded — {} verts, {} clips",
                asset.mesh.positions.size(),
                asset.clips.size());
    return asset;
}

} // namespace engine::character
