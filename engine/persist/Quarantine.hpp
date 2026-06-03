#pragma once

#include "engine/core/math.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace engine {

[[nodiscard]] std::filesystem::path quarantine_chunk_path(
    const std::filesystem::path& world_dir,
    ChunkCoord coord);

[[nodiscard]] bool quarantine_chunk_blob(
    const std::filesystem::path& world_dir,
    ChunkCoord coord,
    std::span<const std::uint8_t> blob_bytes);

} // namespace engine
