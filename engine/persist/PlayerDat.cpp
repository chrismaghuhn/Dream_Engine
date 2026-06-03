#include "engine/persist/PlayerDat.hpp"

#include "engine/persist/AtomicFile.hpp"

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

void append_f32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
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

[[nodiscard]] bool read_f32(std::span<const std::uint8_t>& bytes, float& out) {
    if (bytes.size() < 4) {
        return false;
    }
    std::uint32_t raw = 0;
    for (int i = 0; i < 4; ++i) {
        raw |= static_cast<std::uint32_t>(bytes[static_cast<size_t>(i)]) << (8 * i);
    }
    std::memcpy(&out, &raw, sizeof(out));
    bytes = bytes.subspan(4);
    return true;
}

} // namespace

std::vector<std::uint8_t> encode_player_dat_v1(const PlayerSaveV1& player) {
    std::vector<std::uint8_t> out;
    out.reserve(64);
    out.insert(out.end(), std::begin(kPlayerDatMagic), std::end(kPlayerDatMagic));
    append_u16(out, kPlayerDatVersion);
    append_i32(out, player.position.chunk.x);
    append_i32(out, player.position.chunk.y);
    append_i32(out, player.position.chunk.z);
    append_f32(out, player.position.local.x);
    append_f32(out, player.position.local.y);
    append_f32(out, player.position.local.z);
    append_f32(out, player.health);
    append_f32(out, player.hunger);
    out.push_back(player.inventory.hotbar_selected);
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
    append_u16(out, kPlayerDatHotbarPersistSlots);
    for (std::size_t i = 0; i < kHotbarSlots; ++i) {
        append_u16(out, player.inventory.hotbar[i].item_id);
        append_u16(out, player.inventory.hotbar[i].count);
    }
    return out;
}

bool decode_player_dat_v1(std::span<const std::uint8_t> bytes, PlayerSaveV1& out) {
    if (bytes.size() < 4 || std::memcmp(bytes.data(), kPlayerDatMagic, 4) != 0) {
        return false;
    }
    bytes = bytes.subspan(4);

    std::uint16_t version = 0;
    if (!read_u16(bytes, version) || version != kPlayerDatVersion) {
        return false;
    }

    PlayerSaveV1 player{};
    if (!read_i32(bytes, player.position.chunk.x) ||
        !read_i32(bytes, player.position.chunk.y) ||
        !read_i32(bytes, player.position.chunk.z) ||
        !read_f32(bytes, player.position.local.x) ||
        !read_f32(bytes, player.position.local.y) ||
        !read_f32(bytes, player.position.local.z) ||
        !read_f32(bytes, player.health) ||
        !read_f32(bytes, player.hunger)) {
        return false;
    }

    if (bytes.empty()) {
        return false;
    }
    player.inventory.hotbar_selected = bytes[0];
    bytes = bytes.subspan(1);
    if (bytes.size() < 3) {
        return false;
    }
    bytes = bytes.subspan(3);

    std::uint16_t inventory_slots = 0;
    if (!read_u16(bytes, inventory_slots)) {
        return false;
    }

    if (inventory_slots == 0) {
        out = player;
        return true;
    }

    if (inventory_slots != kPlayerDatHotbarPersistSlots) {
        return false;
    }

    const size_t inventory_bytes = static_cast<size_t>(inventory_slots) * 4;
    if (bytes.size() < inventory_bytes) {
        return false;
    }

    for (std::size_t i = 0; i < kHotbarSlots; ++i) {
        std::uint16_t item_id = 0;
        std::uint16_t count = 0;
        if (!read_u16(bytes, item_id) || !read_u16(bytes, count)) {
            return false;
        }
        player.inventory.hotbar[i].item_id = item_id;
        player.inventory.hotbar[i].count = count;
    }

    out = player;
    return true;
}

bool write_player_dat_atomic(const std::filesystem::path& path, const PlayerSaveV1& player) {
    const std::vector<std::uint8_t> bytes = encode_player_dat_v1(player);
    return atomic_write_file(path, bytes);
}

bool read_player_dat(const std::filesystem::path& path, PlayerSaveV1& out) {
    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(path, bytes)) {
        return false;
    }
    return decode_player_dat_v1(bytes, out);
}

} // namespace engine
