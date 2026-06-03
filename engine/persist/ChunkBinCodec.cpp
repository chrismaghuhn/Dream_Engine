#include "engine/persist/ChunkBinCodec.hpp"

#include "engine/world/SectionIndexing.hpp"

#include <cstring>
#include <span>

namespace engine {

namespace {

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

} // namespace

std::vector<std::uint8_t> encode_chunk_bin(const Chunk& chunk) {
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

bool decode_chunk_bin(std::span<const std::uint8_t> bytes, Chunk& chunk) {
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

} // namespace engine
