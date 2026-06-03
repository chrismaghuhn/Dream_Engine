#include "engine/world/OriginRebase.hpp"

#include "engine/world/WorldEvents.hpp"

namespace engine {

namespace {

glm::vec3 aligned_render_origin(glm::vec3 world_pos, float rebase_radius) {
    return glm::floor(world_pos / rebase_radius) * rebase_radius;
}

} // namespace

bool OriginRebase::maybe_rebase(
    flecs::world& ecs, glm::vec3 camera_world_pos, const WorldConfig& config) {
    const float radius = config.rebase_radius;
    if (radius <= 0.f) {
        return false;
    }

    if (glm::length(camera_world_pos - render_origin_) <= radius) {
        return false;
    }

    const glm::vec3 new_origin = aligned_render_origin(camera_world_pos, radius);
    if (new_origin == render_origin_) {
        return false;
    }

    render_origin_ = new_origin;

    const EvtOriginShift payload{new_origin};
    flecs::entity source = ecs.lookup("WorldRoot");
    if (!source) {
        source = ecs.entity("WorldRoot").add<WorldRoot>();
    }
    ecs.event<EvtOriginShift>().id<WorldRoot>().entity(source).ctx(payload).emit();
    return true;
}

} // namespace engine
