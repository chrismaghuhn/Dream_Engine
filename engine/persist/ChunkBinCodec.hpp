#pragma once

#include "engine/world/Chunk.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace engine {

constexpr char kChunkBinMagic[4] = {'V', 'C', 'H', '1'};
constexpr std::uint16_t kChunkBinVersion = 1;

[[nodiscard]] std::vector<std::uint8_t> encode_chunk_bin(const Chunk& chunk);
[[nodiscard]] bool decode_chunk_bin(std::span<const std::uint8_t> bytes, Chunk& chunk);

} // namespace engine
