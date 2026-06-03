#pragma once

#include "engine/movement/core/TextParser.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace engine::movement {

class MovementWorld;

// Save/load of runtime overrides on top of an arena definition. Saves reference
// an arena by id and apply per-PersistentId overrides; loading rejects unknown
// PersistentIds and resets previous_transform = current_transform.
class SaveService {
public:
    [[nodiscard]] static std::filesystem::path default_save_path();

    // Serialize every player entity (those with a PlayerController) as an
    // override block: transform, player_controller runtime, camera_rig.
    [[nodiscard]] static std::string serialize(const MovementWorld& world,
                                               std::string_view slot,
                                               std::string_view arena_id);

    static void save(const std::filesystem::path& path,
                     const MovementWorld& world,
                     std::string_view slot,
                     std::string_view arena_id);

    // Apply a save document's overrides to an already-populated world. Throws if
    // arena_id mismatches or an override targets an unknown PersistentId.
    static void apply_document(const SaveDocument& doc,
                               MovementWorld& world,
                               std::string_view expected_arena_id);

    static void load_from_string(std::string_view text,
                                 std::string_view source,
                                 MovementWorld& world,
                                 std::string_view expected_arena_id);

    static void load(const std::filesystem::path& path,
                     MovementWorld& world,
                     std::string_view expected_arena_id);
};

} // namespace engine::movement
