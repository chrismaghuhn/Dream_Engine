#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::movement {

// Stable identity for an arena entity that survives save/load. The runtime key
// is a 64-bit FNV-1a hash of "<arena_id>/<entity_name>"; the original string is
// retained for save-file readability and error messages.
class PersistentId {
public:
    PersistentId() = default;

    static PersistentId make(std::string_view arena_id, std::string_view entity_name) {
        std::string composed;
        composed.reserve(arena_id.size() + 1 + entity_name.size());
        composed.append(arena_id);
        composed.push_back('/');
        composed.append(entity_name);
        return PersistentId(std::move(composed));
    }

    static PersistentId from_string(std::string composed) {
        return PersistentId(std::move(composed));
    }

    [[nodiscard]] std::uint64_t hash() const { return hash_; }
    [[nodiscard]] const std::string& str() const { return str_; }

    friend bool operator==(const PersistentId& lhs, const PersistentId& rhs) {
        return lhs.hash_ == rhs.hash_;
    }
    friend bool operator!=(const PersistentId& lhs, const PersistentId& rhs) {
        return lhs.hash_ != rhs.hash_;
    }

    static std::uint64_t fnv1a(std::string_view text) {
        constexpr std::uint64_t kOffsetBasis = 1469598103934665603ull;
        constexpr std::uint64_t kPrime = 1099511628211ull;
        std::uint64_t hash = kOffsetBasis;
        for (const char c : text) {
            hash ^= static_cast<std::uint8_t>(c);
            hash *= kPrime;
        }
        return hash;
    }

private:
    explicit PersistentId(std::string composed)
        : str_(std::move(composed)), hash_(fnv1a(str_)) {}

    std::string str_;
    std::uint64_t hash_ = 0;
};

} // namespace engine::movement
