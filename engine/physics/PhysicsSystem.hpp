#pragma once

#include "engine/physics/ObjectLayerMatrix.hpp"

#include <glm/glm.hpp>
#include <cstdint>

namespace engine {

struct DebrisBodyHandle {
    bool valid = false;
    std::uint32_t body_index = 0;
    std::uint32_t body_sequence = 0;
};

class PhysicsSystem {
public:
    [[nodiscard]] bool init();
    void shutdown();

    [[nodiscard]] bool is_active() const { return active_; }

    [[nodiscard]] DebrisBodyHandle create_debris_box(const glm::vec3& center, float half_extent = 0.5f);
    void destroy_debris_body(DebrisBodyHandle& handle);

    [[nodiscard]] ObjectLayer debris_body_layer(const DebrisBodyHandle& handle) const;

private:
    bool active_ = false;
};

} // namespace engine
