#pragma once

#include "engine/character/core/CharacterAsset.hpp"

#include <filesystem>
#include <string>

namespace engine::character {

class GltfIngest {
public:
    // Load mesh, skeleton, bind pose, and base color texture from a Character_output GLB.
    [[nodiscard]] static CharacterAsset load_base(const std::string& glb_path);

    // Extract animation channels from an animation GLB and append them to the asset.
    // Validates skeleton compatibility against asset.mesh.bones; throws on mismatch.
    static void load_animation_clip(CharacterAsset& asset,
                                    const std::string& anim_glb_path,
                                    const std::string& clip_name);
};

} // namespace engine::character
