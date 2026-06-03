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

    // Load base mesh from cache or disk.
    CharacterAsset asset = CookedCharacterCache::load_or_cook(base_path, [&] {
        return GltfIngest::load_base(base_path);
    });

    // Animation clip list: (file suffix, logical clip name)
    const std::pair<std::string, std::string> clips[] = {
        {"Animation_Walking_withSkin.glb",     "Walk"},
        {"Animation_Running_withSkin.glb",     "Run"},
        {"Animation_High_Kick_withSkin.glb",   "High_Kick"},
        {"Animation_Elbow_Strike_withSkin.glb","Elbow_Strike"},
        {"Animation_Counterstrike_withSkin.glb","Counterstrike"},
    };

    for (const auto& [suffix, clip_name] : clips) {
        const std::string anim_path = player_glb(suffix);
        require_exists(anim_path);
        // Use a per-animation cache key that includes both base and animation path.
        const std::string cache_key = base_path + "|" + anim_path + "|" + clip_name;
        CharacterAsset anim_tmp = CookedCharacterCache::load_or_cook(cache_key, [&] {
            CharacterAsset tmp = GltfIngest::load_base(base_path);
            GltfIngest::load_animation_clip(tmp, anim_path, clip_name);
            return tmp;
        });
        if (!anim_tmp.clips.empty()) {
            asset.clips.push_back(std::move(anim_tmp.clips.back()));
        }
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
