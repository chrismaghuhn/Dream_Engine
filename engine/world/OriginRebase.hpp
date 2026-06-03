#pragma once

#include "engine/world/WorldConfig.hpp"

#include <flecs.h>
#include <glm/glm.hpp>

namespace engine {

class OriginRebase {
public:
    [[nodiscard]] glm::vec3 render_origin() const { return render_origin_; }

    bool maybe_rebase(flecs::world& ecs, glm::vec3 camera_world_pos, const WorldConfig& config);

private:
    glm::vec3 render_origin_{0.f};
};

} // namespace engine
