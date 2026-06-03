#pragma once

#include <string>
#include <unordered_map>

namespace engine::character {

struct AttackDef {
    std::string id;
    std::string clip;
    float hit_start_norm = 0.f;
    float hit_end_norm = 0.f;
    float range = 0.f;
    float radius = 0.f;
    float recovery_seconds = 0.2f;
    float cancel_start_norm = 0.7f;
    float dodge_cancel_start_norm = 0.6f;
};

using AttackTable = std::unordered_map<std::string, AttackDef>;

class AttackData {
public:
    [[nodiscard]] static AttackTable load(const std::string& path);
};

} // namespace engine::character
