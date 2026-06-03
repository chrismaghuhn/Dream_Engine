#pragma once

#include "engine/character/core/CharacterAsset.hpp"

namespace engine::character {

class CharacterCatalog {
public:
    // Loads base mesh + all v1 locomotion + combo clips for the Dungeon Explorer.
    // Uses CookedCharacterCache for each GLB; throws on validation failure.
    [[nodiscard]] static CharacterAsset load_player_set();

    // Loads Straw Fantasy biped: base mesh + Hit_Reaction_1 clip.
    [[nodiscard]] static CharacterAsset load_dummy_set();
};

} // namespace engine::character
