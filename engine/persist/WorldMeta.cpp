#include "engine/persist/WorldMeta.hpp"

#include "engine/persist/AtomicFile.hpp"

#include <fstream>
#include <sstream>

namespace engine {

bool write_world_meta_atomic(const std::filesystem::path& world_dir, const WorldMeta& meta) {
    std::ostringstream text;
    text << "format=" << meta.format << '\n';
    text << "version=" << meta.version << '\n';
    text << "seed=" << meta.world_seed << '\n';
    if (meta.minimal_migrated) {
        text << "minimal_migrated=true\n";
    }
    return atomic_write_file(world_dir / "world.meta", text.str());
}

bool read_world_meta(const std::filesystem::path& world_dir, WorldMeta& out) {
    std::ifstream in(world_dir / "world.meta");
    if (!in) {
        return false;
    }

    WorldMeta meta{};
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "format") {
            meta.format = value;
        } else if (key == "version") {
            meta.version = static_cast<std::uint32_t>(std::stoul(value));
        } else if (key == "seed") {
            meta.world_seed = std::stoull(value);
        } else if (key == "minimal_migrated") {
            meta.minimal_migrated = (value == "true" || value == "1");
        }
    }

    if (meta.format != "minimal_m5" && meta.format != "region_m8") {
        return false;
    }

    out = meta;
    return true;
}

bool world_meta_exists(const std::filesystem::path& world_dir) {
    std::error_code ec;
    return std::filesystem::exists(world_dir / "world.meta", ec);
}

bool uses_minimal_save_backend(const WorldMeta& meta) {
    return meta.format == "minimal_m5" && !meta.minimal_migrated;
}

} // namespace engine
