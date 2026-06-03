#include "engine/persist/AtomicFile.hpp"

#include <fstream>

namespace engine {

namespace {

[[nodiscard]] bool write_file_raw(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return out.good();
}

} // namespace

bool atomic_write_file(const std::filesystem::path& target, std::span<const std::uint8_t> bytes) {
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);

    const std::filesystem::path temp = target.string() + ".tmp";
    ec.clear();
    std::filesystem::remove(temp, ec);

    if (!write_file_raw(temp, bytes)) {
        std::filesystem::remove(temp, ec);
        return false;
    }

    ec.clear();
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

bool atomic_write_file(const std::filesystem::path& target, const std::string& text) {
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    return atomic_write_file(target, std::span<const std::uint8_t>(data, text.size()));
}

bool read_file_bytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }

    const auto size = in.tellg();
    if (size < 0) {
        return false;
    }

    out.resize(static_cast<size_t>(size));
    in.seekg(0, std::ios::beg);
    if (size > 0) {
        in.read(reinterpret_cast<char*>(out.data()), size);
    }
    return in.good();
}

} // namespace engine
