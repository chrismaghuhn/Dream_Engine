#include "engine/persist/MinimalSaveBackend.hpp"

#include "engine/persist/AtomicFile.hpp"
#include "engine/world/Chunk.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace engine {

namespace {

constexpr char kChunkBinMagic[4] = {'V', 'C', 'H', '1'};
constexpr std::uint16_t kChunkBinVersion = 1;

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

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void append_i32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((raw >> (8 * i)) & 0xFFu));
    }
}

[[nodiscard]] bool read_u16(std::span<const std::uint8_t>& bytes, std::uint16_t& out) {
    if (bytes.size() < 2) {
        return false;
    }
    out = static_cast<std::uint16_t>(bytes[0]) | (static_cast<std::uint16_t>(bytes[1]) << 8);
    bytes = bytes.subspan(2);
    return true;
}

[[nodiscard]] bool read_i32(std::span<const std::uint8_t>& bytes, std::int32_t& out) {
    if (bytes.size() < 4) {
        return false;
    }
    std::uint32_t raw = 0;
    for (int i = 0; i < 4; ++i) {
        raw |= static_cast<std::uint32_t>(bytes[static_cast<size_t>(i)]) << (8 * i);
    }
    out = static_cast<std::int32_t>(raw);
    bytes = bytes.subspan(4);
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> encode_section(const Section& section) {
    std::vector<std::uint8_t> out;
    out.reserve(8192 + section.palette.size() * 2);
    const auto palette_count = static_cast<std::uint16_t>(section.palette.size());
    append_u16(out, palette_count);
    for (const BlockState& state : section.palette) {
        append_u16(out, state.raw);
    }
    for (const std::uint16_t block_idx : section.blocks) {
        append_u16(out, block_idx);
    }
    return out;
}

[[nodiscard]] bool decode_section(std::span<const std::uint8_t>& bytes, Section& section) {
    std::uint16_t palette_count = 0;
    if (!read_u16(bytes, palette_count)) {
        return false;
    }

    section = Section{};
    section.palette.clear();
    section.palette.reserve(palette_count);
    for (std::uint16_t i = 0; i < palette_count; ++i) {
        std::uint16_t raw = 0;
        if (!read_u16(bytes, raw)) {
            return false;
        }
        section.palette.push_back(BlockState{raw});
    }

    for (std::uint16_t& block_idx : section.blocks) {
        if (!read_u16(bytes, block_idx)) {
            return false;
        }
    }

    section.sync_occupancy_from_blocks();
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> encode_chunk_bin(const Chunk& chunk) {
    std::vector<std::uint8_t> out;
    out.reserve(65536);
    out.insert(out.end(), std::begin(kChunkBinMagic), std::end(kChunkBinMagic));
    append_u16(out, kChunkBinVersion);
    out.push_back(chunk.flags);
    out.push_back(0);
    append_i32(out, chunk.coord.x);
    append_i32(out, chunk.coord.y);
    append_i32(out, chunk.coord.z);

    for (const Section& section : chunk.sections) {
        const std::vector<std::uint8_t> section_bytes = encode_section(section);
        out.insert(out.end(), section_bytes.begin(), section_bytes.end());
    }
    return out;
}

[[nodiscard]] bool decode_chunk_bin(std::span<const std::uint8_t> bytes, Chunk& chunk) {
    if (bytes.size() < 4 || std::memcmp(bytes.data(), kChunkBinMagic, 4) != 0) {
        return false;
    }
    bytes = bytes.subspan(4);

    std::uint16_t version = 0;
    if (!read_u16(bytes, version) || version != kChunkBinVersion) {
        return false;
    }
    if (bytes.size() < 14) {
        return false;
    }

    chunk.flags = bytes[0];
    bytes = bytes.subspan(2);

    ChunkCoord coord{};
    if (!read_i32(bytes, coord.x) || !read_i32(bytes, coord.y) || !read_i32(bytes, coord.z)) {
        return false;
    }
    chunk.coord = coord;

    for (Section& section : chunk.sections) {
        if (!decode_section(bytes, section)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool write_world_meta_atomic(
    const std::filesystem::path& world_dir,
    const MinimalSaveWorldMeta& meta) {
    std::ostringstream text;
    text << "format=" << meta.format << '\n';
    text << "version=" << meta.version << '\n';
    text << "seed=" << meta.world_seed << '\n';
    return atomic_write_file(world_dir / "world.meta", text.str());
}

[[nodiscard]] bool read_world_meta(
    const std::filesystem::path& world_dir,
    MinimalSaveWorldMeta& out) {
    std::ifstream in(world_dir / "world.meta");
    if (!in) {
        return false;
    }

    MinimalSaveWorldMeta meta{};
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
        }
    }

    if (meta.format != "minimal_m5") {
        return false;
    }

    out = meta;
    return true;
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

    const MinimalSaveWorldMeta meta{
        .world_seed = world_config.world_seed,
        .format = "minimal_m5",
        .version = 1,
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
    MinimalSaveWorldMeta meta{};
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
