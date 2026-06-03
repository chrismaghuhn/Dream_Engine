#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace engine {

[[nodiscard]] bool atomic_write_file(
    const std::filesystem::path& target,
    std::span<const std::uint8_t> bytes);

[[nodiscard]] bool atomic_write_file(
    const std::filesystem::path& target,
    const std::string& text);

[[nodiscard]] bool read_file_bytes(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& out);

} // namespace engine
