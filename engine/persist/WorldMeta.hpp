#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace engine {

struct WorldMeta {
    std::uint64_t world_seed = 0;
    std::string format = "region_m8";
    std::uint32_t version = 1;
    bool minimal_migrated = false;
};

[[nodiscard]] bool write_world_meta_atomic(
    const std::filesystem::path& world_dir,
    const WorldMeta& meta);

[[nodiscard]] bool read_world_meta(
    const std::filesystem::path& world_dir,
    WorldMeta& out);

[[nodiscard]] bool world_meta_exists(const std::filesystem::path& world_dir);

[[nodiscard]] bool uses_minimal_save_backend(const WorldMeta& meta);

} // namespace engine
