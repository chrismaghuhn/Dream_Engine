#pragma once

#include "engine/movement/core/EntityWorld.hpp"
#include "engine/movement/core/TextParser.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace engine::movement {

class MovementWorld;

// Result of loading an arena definition into a MovementWorld.
struct ArenaLoadResult {
    std::string arena_id;
    long version = 0;
    EntityId player{}; // first entity carrying a PlayerController, or null
};

// Parse + populate. All throw ParseException (with file:line:col) on malformed
// input, unknown components/fields, missing required fields, or invalid shapes.
ArenaLoadResult load_arena_document(const ArenaDocument& doc, MovementWorld& world);
ArenaLoadResult load_arena_from_string(std::string_view text,
                                       std::string_view source,
                                       MovementWorld& world);
ArenaLoadResult load_arena_file(const std::filesystem::path& path, MovementWorld& world);

} // namespace engine::movement
