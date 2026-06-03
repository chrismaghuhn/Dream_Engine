#include "engine/persist/MinimalSaveBackend.hpp"

#include "engine/persist/AtomicFile.hpp"
#include "engine/persist/ChunkBinCodec.hpp"
#include "engine/persist/PlayerDat.hpp"
#include "engine/persist/WorldMeta.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace engine {

namespace {

[[nodiscard]] std::filesystem::path chunks_dir(const std::filesystem::path& world_dir) {
    return world_dir / "minimal" / "chunks";
}

[[nodiscard]] std::string chunk_filename(ChunkCoord coord) {
    char buffer[64];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "c.%d.%d.%d.bin",
        coord.x,
        coord.y,
        coord.z);
    return buffer;
}

[[nodiscard]] bool parse_chunk_filename(const std::string& filename, ChunkCoord& out) {
    int cx = 0;
    int cy = 0;
    int cz = 0;
    if (std::sscanf(filename.c_str(), "c.%d.%d.%d.bin", &cx, &cy, &cz) != 3) {
        return false;
    }
    out = {cx, cy, cz};
    return true;
}

} // namespace

std::filesystem::path minimal_save_world_dir(
    const std::filesystem::path& saves_root,
    const std::string& world_name) {
    return saves_root / world_name;
}

bool minimal_save_world(
    const std::filesystem::path& world_dir,
    const WorldConfig& world_config,
    const WorldPosition& player_position,
    const InventorySnapshot& inventory,
    ChunkStore& store) {
    std::error_code ec;
    std::filesystem::create_directories(chunks_dir(world_dir), ec);

    const WorldMeta meta{
        .world_seed = world_config.world_seed,
        .format = "minimal_m5",
        .version = 1,
        .minimal_migrated = false,
    };
    if (!write_world_meta_atomic(world_dir, meta)) {
        return false;
    }

    store.for_each_loaded([&](ChunkCoord coord) {
        const Chunk* chunk = store.try_get(coord);
        if (chunk == nullptr || (chunk->flags & CHUNK_MODIFIED_BY_PLAYER) == 0) {
            return;
        }

        const std::vector<std::uint8_t> bytes = encode_chunk_bin(*chunk);
        const std::filesystem::path path = chunks_dir(world_dir) / chunk_filename(coord);
        (void)atomic_write_file(path, bytes);
    });

    const PlayerSaveV1 player{
        .position = player_position,
        .inventory = inventory,
    };
    return write_player_dat_atomic(world_dir / "player.dat", player);
}

bool minimal_load_world(
    const std::filesystem::path& world_dir,
    const WorldConfig& world_config,
    WorldPosition& out_player_position,
    InventorySnapshot& out_inventory,
    ChunkStore& store) {
    WorldMeta meta{};
    if (!read_world_meta(world_dir, meta)) {
        return false;
    }
    if (meta.world_seed != world_config.world_seed) {
        return false;
    }

    PlayerSaveV1 player{};
    if (!read_player_dat(world_dir / "player.dat", player)) {
        return false;
    }
    out_player_position = player.position;
    out_inventory = player.inventory;

    const std::filesystem::path chunk_root = chunks_dir(world_dir);
    std::error_code ec;
    if (!std::filesystem::exists(chunk_root, ec)) {
        return true;
    }

    for (const auto& entry : std::filesystem::directory_iterator(chunk_root, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }

        ChunkCoord coord{};
        if (!parse_chunk_filename(entry.path().filename().string(), coord)) {
            continue;
        }

        std::vector<std::uint8_t> bytes;
        if (!read_file_bytes(entry.path(), bytes)) {
            continue;
        }

        Chunk decoded{};
        if (!decode_chunk_bin(bytes, decoded)) {
            continue;
        }

        Chunk* chunk = store.try_get(coord);
        if (chunk == nullptr) {
            chunk = store.allocate(coord);
        }
        if (chunk == nullptr) {
            continue;
        }

        *chunk = decoded;
        chunk->coord = coord;
        chunk->flags |= CHUNK_MODIFIED_BY_PLAYER;
    }

    return true;
}

} // namespace engine
