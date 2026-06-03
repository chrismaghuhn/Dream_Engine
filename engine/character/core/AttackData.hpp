#pragma once

#include <string>
#include <unordered_map>

namespace engine::character {

struct AttackDef {
    std::string id;
    std::string clip;
    float hit_start = 0.f;
    float hit_end = 0.f;
    float range = 0.f;
    float radius = 0.f;
    float recovery = 0.f;
};

using AttackTable = std::unordered_map<std::string, AttackDef>;

class AttackData {
public:
    [[nodiscard]] static AttackTable load(const std::string& path);
};

} // namespace engine::character
