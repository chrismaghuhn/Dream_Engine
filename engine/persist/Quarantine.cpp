#include "engine/persist/Quarantine.hpp"

#include "engine/persist/AtomicFile.hpp"

#include <cstdio>
#include <string>

namespace engine {

namespace {

[[nodiscard]] std::string chunk_blob_filename(ChunkCoord coord) {
    char buffer[64];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "c.%d.%d.%d.blob",
        coord.x,
        coord.y,
        coord.z);
    return buffer;
}

} // namespace

std::filesystem::path quarantine_chunk_path(
    const std::filesystem::path& world_dir,
    ChunkCoord coord) {
    return world_dir / "quarantine" / "chunks" / chunk_blob_filename(coord);
}

bool quarantine_chunk_blob(
    const std::filesystem::path& world_dir,
    ChunkCoord coord,
    std::span<const std::uint8_t> blob_bytes) {
    const std::filesystem::path path = quarantine_chunk_path(world_dir, coord);
    return atomic_write_file(path, blob_bytes);
}

} // namespace engine
