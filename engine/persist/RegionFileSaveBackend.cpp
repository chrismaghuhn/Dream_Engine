#include "engine/persist/RegionFileSaveBackend.hpp"

#include "engine/persist/AtomicFile.hpp"
#include "engine/persist/ChunkBinCodec.hpp"
#include "engine/persist/PlayerDat.hpp"
#include "engine/persist/Quarantine.hpp"
#include "engine/persist/WorldMeta.hpp"
#include "engine/world/ChunkStore.hpp"

#include <zstd.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <unordered_map>

namespace engine {

namespace {

constexpr std::size_t kVwrHeaderBytes = 16;
constexpr std::size_t kVwrIndexEntries = 512;
constexpr std::size_t kVwrIndexEntryBytes = 8;
constexpr std::size_t kVwrIndexBytes = kVwrIndexEntries * kVwrIndexEntryBytes;
constexpr std::size_t kVwrDataStartSector = 2;
constexpr std::size_t kChunkBlobHeaderBytes = 16;

struct VwrChunkIndexEntry {
    std::uint32_t offset_sectors = 0;
    std::uint32_t blob_size_bytes = 0;
    std::uint8_t flags = 0;
};

struct ChunkBlobPayload {
    std::vector<std::uint8_t> uncompressed;
    std::uint8_t flags = 0;
};

[[nodiscard]] std::uint32_t crc32_ieee(std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void write_u24_at(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
    out[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    out[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
}

[[nodiscard]] std::uint32_t read_u24(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset])
         | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
         | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16);
}

void write_index_entry(std::vector<std::uint8_t>& index, int slot, const VwrChunkIndexEntry& entry) {
    const std::size_t base = static_cast<std::size_t>(slot) * kVwrIndexEntryBytes;
    write_u24_at(index, base, entry.offset_sectors);
    const std::uint32_t size20 = entry.blob_size_bytes & 0xFFFFFu;
    index[base + 3] = static_cast<std::uint8_t>(size20 & 0xFFu);
    index[base + 4] = static_cast<std::uint8_t>((size20 >> 8) & 0xFFu);
    index[base + 5] = static_cast<std::uint8_t>((size20 >> 16) & 0x0Fu);
    index[base + 6] = entry.flags;
    index[base + 7] = 0;
}

[[nodiscard]] VwrChunkIndexEntry read_index_entry(std::span<const std::uint8_t> index, int slot) {
    const std::size_t base = static_cast<std::size_t>(slot) * kVwrIndexEntryBytes;
    VwrChunkIndexEntry entry{};
    entry.offset_sectors = read_u24(index, base);
    entry.blob_size_bytes =
        static_cast<std::uint32_t>(index[base + 3])
        | (static_cast<std::uint32_t>(index[base + 4]) << 8)
        | ((static_cast<std::uint32_t>(index[base + 5]) & 0x0Fu) << 16);
    entry.flags = index[base + 6];
    return entry;
}

[[nodiscard]] std::filesystem::path regions_dir(const std::filesystem::path& world_dir) {
    return world_dir / "regions";
}

[[nodiscard]] std::string region_filename(RegionCoord region) {
    char buffer[64];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "r.%d.%d.%d.vwr",
        region.x,
        region.y,
        region.z);
    return buffer;
}

[[nodiscard]] std::filesystem::path minimal_chunks_dir(const std::filesystem::path& world_dir) {
    return world_dir / "minimal" / "chunks";
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

[[nodiscard]] std::size_t sector_align(std::size_t bytes) {
    return (bytes + kVwrSectorSize - 1) / kVwrSectorSize;
}

[[nodiscard]] std::vector<std::uint8_t> zstd_compress_bytes(std::span<const std::uint8_t> input) {
    const std::size_t bound = ZSTD_compressBound(input.size());
    std::vector<std::uint8_t> out(bound);
    const std::size_t written = ZSTD_compress(
        out.data(),
        out.size(),
        input.data(),
        input.size(),
        ZSTD_CLEVEL_DEFAULT);
    if (ZSTD_isError(written)) {
        return {};
    }
    out.resize(written);
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> zstd_decompress_bytes(
    std::span<const std::uint8_t> compressed,
    std::size_t uncompressed_size) {
    std::vector<std::uint8_t> out(uncompressed_size);
    const std::size_t written = ZSTD_decompress(
        out.data(),
        out.size(),
        compressed.data(),
        compressed.size());
    if (ZSTD_isError(written) || written != uncompressed_size) {
        return {};
    }
    return out;
}

void compress_chunks_on_io(
    JobSystem* jobs,
    std::vector<ChunkBlobPayload>& payloads,
    std::vector<std::vector<std::uint8_t>>& out_compressed) {
    out_compressed.resize(payloads.size());
    if (payloads.empty()) {
        return;
    }

    if (jobs == nullptr) {
        for (std::size_t i = 0; i < payloads.size(); ++i) {
            out_compressed[i] = zstd_compress_bytes(payloads[i].uncompressed);
        }
        return;
    }

    std::atomic<std::size_t> completed{0};
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        jobs->run_io([&payloads, &out_compressed, i, &completed]() {
            out_compressed[i] = zstd_compress_bytes(payloads[i].uncompressed);
            completed.fetch_add(1);
        });
    }
    while (completed.load() < payloads.size()) {
        jobs->wait_all();
    }
}

[[nodiscard]] std::vector<std::uint8_t> build_chunk_blob(
    std::span<const std::uint8_t> uncompressed,
    std::span<const std::uint8_t> compressed,
    std::uint8_t flags) {
    std::vector<std::uint8_t> blob;
    blob.resize(kChunkBlobHeaderBytes + compressed.size());

    const auto uncompressed_size = static_cast<std::uint32_t>(uncompressed.size());
    const auto compressed_size = static_cast<std::uint32_t>(compressed.size());
    const auto crc = crc32_ieee(uncompressed);

    std::memcpy(blob.data() + 0, &uncompressed_size, 4);
    std::memcpy(blob.data() + 4, &compressed_size, 4);
    std::memcpy(blob.data() + 8, &crc, 4);
    const std::uint16_t version = kCurrentChunkBlobVersion;
    std::memcpy(blob.data() + 12, &version, 2);
    blob[14] = flags;
    blob[15] = 0;
    if (!compressed.empty()) {
        std::memcpy(blob.data() + kChunkBlobHeaderBytes, compressed.data(), compressed.size());
    }
    return blob;
}

[[nodiscard]] bool write_region_file_atomic(
    const std::filesystem::path& path,
    RegionCoord region,
    const std::unordered_map<int, std::vector<std::uint8_t>>& slot_blobs) {
    std::vector<std::uint8_t> file;
    file.resize(kVwrDataStartSector * kVwrSectorSize, 0);

    std::memcpy(file.data(), kVwrMagic, 4);
    const std::uint16_t version = kVwrFileVersion;
    std::memcpy(file.data() + 4, &version, 2);
    file[6] = kVwrCompressionZstd;
    file[7] = kVwrSectorShift;

    std::vector<std::uint8_t> index(kVwrIndexBytes, 0);
    std::uint32_t next_sector = static_cast<std::uint32_t>(kVwrDataStartSector);

    std::vector<int> slots;
    slots.reserve(slot_blobs.size());
    for (const auto& [slot, blob] : slot_blobs) {
        slots.push_back(slot);
    }
    std::sort(slots.begin(), slots.end());

    for (const int slot : slots) {
        const auto it = slot_blobs.find(slot);
        if (it == slot_blobs.end() || it->second.empty()) {
            continue;
        }

        const std::size_t blob_offset = static_cast<std::size_t>(next_sector) * kVwrSectorSize;
        if (file.size() < blob_offset) {
            file.resize(blob_offset, 0);
        }
        if (file.size() < blob_offset + it->second.size()) {
            file.resize(blob_offset + it->second.size());
        }
        std::memcpy(file.data() + blob_offset, it->second.data(), it->second.size());

        VwrChunkIndexEntry entry{};
        entry.offset_sectors = next_sector;
        entry.blob_size_bytes = static_cast<std::uint32_t>(it->second.size());
        write_index_entry(index, slot, entry);

        const std::size_t padded_sectors = sector_align(it->second.size());
        next_sector += static_cast<std::uint32_t>(padded_sectors);
    }

    std::memcpy(file.data() + kVwrHeaderBytes, index.data(), index.size());
    return atomic_write_file(path, file);
}

[[nodiscard]] bool merge_slot_into_region_file(
    const std::filesystem::path& region_path,
    RegionCoord region,
    int slot,
    std::vector<std::uint8_t> blob) {
    std::unordered_map<int, std::vector<std::uint8_t>> slots;

    std::vector<std::uint8_t> existing;
    if (read_file_bytes(region_path, existing) && existing.size() >= kVwrHeaderBytes + kVwrIndexBytes) {
        if (std::memcmp(existing.data(), kVwrMagic, 4) == 0) {
            const std::span<const std::uint8_t> index_span(
                existing.data() + kVwrHeaderBytes,
                kVwrIndexBytes);
            for (int i = 0; i < static_cast<int>(kVwrIndexEntries); ++i) {
                const VwrChunkIndexEntry entry = read_index_entry(index_span, i);
                if (entry.offset_sectors == 0 || entry.blob_size_bytes == 0) {
                    continue;
                }
                const std::size_t offset =
                    static_cast<std::size_t>(entry.offset_sectors) * kVwrSectorSize;
                if (offset + entry.blob_size_bytes > existing.size()) {
                    continue;
                }
                std::vector<std::uint8_t> prior(entry.blob_size_bytes);
                std::memcpy(
                    prior.data(),
                    existing.data() + offset,
                    entry.blob_size_bytes);
                slots[i] = std::move(prior);
            }
        }
    }

    slots[slot] = std::move(blob);
    return write_region_file_atomic(region_path, region, slots);
}

enum class ChunkLoadOutcome { Ok, Corrupt, VersionMismatch };

[[nodiscard]] ChunkLoadOutcome decode_blob_into_chunk(
    const std::filesystem::path& world_dir,
    ChunkCoord coord,
    std::span<const std::uint8_t> blob,
    Chunk& out_chunk) {
    if (blob.size() < kChunkBlobHeaderBytes) {
        return ChunkLoadOutcome::Corrupt;
    }

    std::uint32_t uncompressed_size = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t expected_crc = 0;
    std::uint16_t chunk_version = 0;
    std::uint8_t flags = 0;
    std::memcpy(&uncompressed_size, blob.data(), 4);
    std::memcpy(&compressed_size, blob.data() + 4, 4);
    std::memcpy(&expected_crc, blob.data() + 8, 4);
    std::memcpy(&chunk_version, blob.data() + 12, 2);
    flags = blob[14];

    if (blob.size() < kChunkBlobHeaderBytes + compressed_size) {
        return ChunkLoadOutcome::Corrupt;
    }

    const std::span<const std::uint8_t> compressed =
        blob.subspan(kChunkBlobHeaderBytes, compressed_size);
    std::vector<std::uint8_t> uncompressed =
        zstd_decompress_bytes(compressed, uncompressed_size);
    if (uncompressed.size() != uncompressed_size) {
        return ChunkLoadOutcome::Corrupt;
    }

    if (crc32_ieee(uncompressed) != expected_crc) {
        if ((flags & CHUNK_MODIFIED_BY_PLAYER) != 0) {
            (void)quarantine_chunk_blob(world_dir, coord, blob);
        }
        return ChunkLoadOutcome::Corrupt;
    }

    if (chunk_version != kCurrentChunkBlobVersion) {
        if ((flags & CHUNK_MODIFIED_BY_PLAYER) != 0) {
            (void)quarantine_chunk_blob(world_dir, coord, blob);
            const std::filesystem::path region_path =
                regions_dir(world_dir) / region_filename(region_of(coord));
            (void)merge_slot_into_region_file(region_path, region_of(coord), local_chunk_index(coord), {});
        }
        return ChunkLoadOutcome::VersionMismatch;
    }

    if (!decode_chunk_bin(uncompressed, out_chunk)) {
        return ChunkLoadOutcome::Corrupt;
    }

    out_chunk.coord = coord;
    out_chunk.flags |= flags;
    return ChunkLoadOutcome::Ok;
}

[[nodiscard]] bool import_minimal_chunk_file(
    const std::filesystem::path& world_dir,
    const std::filesystem::path& chunk_path,
    JobSystem* jobs) {
    ChunkCoord coord{};
    if (!parse_chunk_filename(chunk_path.filename().string(), coord)) {
        return false;
    }

    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(chunk_path, bytes)) {
        return false;
    }

    Chunk chunk{};
    if (!decode_chunk_bin(bytes, chunk)) {
        return false;
    }
    if ((chunk.flags & CHUNK_MODIFIED_BY_PLAYER) == 0) {
        return true;
    }

    ChunkBlobPayload payload{.uncompressed = std::move(bytes), .flags = chunk.flags};
    std::vector<ChunkBlobPayload> batch{payload};
    std::vector<std::vector<std::uint8_t>> compressed;
    compress_chunks_on_io(jobs, batch, compressed);
    if (compressed.empty() || compressed[0].empty()) {
        return false;
    }

    const std::vector<std::uint8_t> blob =
        build_chunk_blob(batch[0].uncompressed, compressed[0], batch[0].flags);
    const RegionCoord region = region_of(coord);
    const std::filesystem::path region_path =
        regions_dir(world_dir) / region_filename(region);
    return merge_slot_into_region_file(region_path, region, local_chunk_index(coord), blob);
}

} // namespace

RegionCoord region_of(ChunkCoord chunk) {
    return {
        floor_div(chunk.x, 8),
        floor_div(chunk.y, 8),
        floor_div(chunk.z, 8),
    };
}

int local_chunk_index(ChunkCoord chunk) {
    const int lx = positive_mod(chunk.x, 8);
    const int ly = positive_mod(chunk.y, 8);
    const int lz = positive_mod(chunk.z, 8);
    return lx + 8 * lz + 64 * ly;
}

std::filesystem::path save_world_dir(
    const std::filesystem::path& saves_root,
    const std::string& world_name) {
    return saves_root / world_name;
}

bool migrate_minimal_to_region(const std::filesystem::path& world_dir, JobSystem* jobs) {
    const std::filesystem::path chunk_root = minimal_chunks_dir(world_dir);
    std::error_code ec;
    if (!std::filesystem::exists(chunk_root, ec)) {
        return true;
    }

    std::filesystem::create_directories(regions_dir(world_dir), ec);

    for (const auto& entry : std::filesystem::directory_iterator(chunk_root, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        (void)import_minimal_chunk_file(world_dir, entry.path(), jobs);
    }

    WorldMeta meta{};
    if (!read_world_meta(world_dir, meta)) {
        meta.format = "region_m8";
        meta.version = 1;
    }
    meta.format = "region_m8";
    meta.minimal_migrated = true;
    return write_world_meta_atomic(world_dir, meta);
}

bool region_save_world(
    const std::filesystem::path& world_dir,
    const WorldConfig& world_config,
    const WorldPosition& player_position,
    const InventorySnapshot& inventory,
    ChunkStore& store,
    JobSystem* jobs) {
    std::error_code ec;
    std::filesystem::create_directories(regions_dir(world_dir), ec);

    std::unordered_map<RegionCoord, std::unordered_map<int, ChunkBlobPayload>, ChunkCoordHash>
        region_payloads;

    store.for_each_loaded([&](ChunkCoord coord) {
        const Chunk* chunk = store.try_get(coord);
        if (chunk == nullptr || (chunk->flags & CHUNK_MODIFIED_BY_PLAYER) == 0) {
            return;
        }
        ChunkBlobPayload payload{
            .uncompressed = encode_chunk_bin(*chunk),
            .flags = chunk->flags,
        };
        region_payloads[region_of(coord)][local_chunk_index(coord)] = std::move(payload);
    });

    if (region_payloads.empty()) {
        return false;
    }

    for (auto& [region, slot_map] : region_payloads) {
        std::vector<int> slots;
        std::vector<ChunkBlobPayload> payloads;
        slots.reserve(slot_map.size());
        payloads.reserve(slot_map.size());
        for (auto& [slot, payload] : slot_map) {
            slots.push_back(slot);
            payloads.push_back(std::move(payload));
        }

        std::vector<std::vector<std::uint8_t>> compressed;
        compress_chunks_on_io(jobs, payloads, compressed);

        std::unordered_map<int, std::vector<std::uint8_t>> slot_blobs;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (i >= compressed.size() || compressed[i].empty()) {
                return false;
            }
            slot_blobs[slots[i]] =
                build_chunk_blob(payloads[i].uncompressed, compressed[i], payloads[i].flags);
        }

        const std::filesystem::path region_path =
            regions_dir(world_dir) / region_filename(region);
        for (const auto& [slot, blob] : slot_blobs) {
            if (!merge_slot_into_region_file(region_path, region, slot, blob)) {
                return false;
            }
        }
    }

    WorldMeta meta{
        .world_seed = world_config.world_seed,
        .format = "region_m8",
        .version = 1,
        .minimal_migrated = true,
    };
    if (!write_world_meta_atomic(world_dir, meta)) {
        return false;
    }

    const PlayerSaveV1 player{
        .position = player_position,
        .inventory = inventory,
    };
    return write_player_dat_atomic(world_dir / "player.dat", player);
}

bool region_load_world(
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

    const std::filesystem::path region_root = regions_dir(world_dir);
    std::error_code ec;
    if (!std::filesystem::exists(region_root, ec)) {
        return true;
    }

    for (const auto& entry : std::filesystem::directory_iterator(region_root, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }

        int rx = 0;
        int ry = 0;
        int rz = 0;
        if (std::sscanf(entry.path().filename().string().c_str(), "r.%d.%d.%d.vwr", &rx, &ry, &rz) != 3) {
            continue;
        }

        std::vector<std::uint8_t> file_bytes;
        if (!read_file_bytes(entry.path(), file_bytes)) {
            continue;
        }
        if (file_bytes.size() < kVwrHeaderBytes + kVwrIndexBytes) {
            continue;
        }
        if (std::memcmp(file_bytes.data(), kVwrMagic, 4) != 0) {
            continue;
        }

        const std::span<const std::uint8_t> index_span(
            file_bytes.data() + kVwrHeaderBytes,
            kVwrIndexBytes);

        for (int slot = 0; slot < static_cast<int>(kVwrIndexEntries); ++slot) {
            const VwrChunkIndexEntry index_entry = read_index_entry(index_span, slot);
            if (index_entry.offset_sectors == 0 || index_entry.blob_size_bytes == 0) {
                continue;
            }

            const int lx = slot % 8;
            const int lz = (slot / 8) % 8;
            const int ly = slot / 64;
            const ChunkCoord coord{
                rx * 8 + lx,
                ry * 8 + ly,
                rz * 8 + lz,
            };

            const std::size_t offset =
                static_cast<std::size_t>(index_entry.offset_sectors) * kVwrSectorSize;
            if (offset + index_entry.blob_size_bytes > file_bytes.size()) {
                continue;
            }

            const std::span<const std::uint8_t> blob(
                file_bytes.data() + offset,
                index_entry.blob_size_bytes);

            Chunk decoded{};
            const ChunkLoadOutcome outcome = decode_blob_into_chunk(world_dir, coord, blob, decoded);
            if (outcome != ChunkLoadOutcome::Ok) {
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
    }

    return true;
}

} // namespace engine
