#pragma once

#include "engine/character/core/CharacterAsset.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace engine::character {

class CookedCharacterCache {
public:
    // Returns the .charbin path for a given source GLB path.
    [[nodiscard]] static std::filesystem::path cache_path_for(const std::string& source_glb);

    // If the cache file is newer than source_glb, loads from cache.
    // Otherwise calls ingest_fn(), writes the result to cache, and returns it.
    [[nodiscard]] static CharacterAsset load_or_cook(
        const std::string& source_glb,
        std::function<CharacterAsset()> ingest_fn);

private:
    static void write(const std::filesystem::path& cache_path, const CharacterAsset& asset);
    [[nodiscard]] static CharacterAsset read(const std::filesystem::path& cache_path);
};

} // namespace engine::character
