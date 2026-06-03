#pragma once

#include "engine/gameplay/Camera.hpp"
#include "engine/platform/Input.hpp"
#include "engine/render/WorldRenderSnapshot.hpp"

#include <cstdint>

#include <flecs.h>

namespace engine {

struct CameraComponent {
    Camera camera{};
};

class CameraSystem {
public:
    static constexpr float kDefaultFlySpeed = 8.f;
    static constexpr float kMouseSensitivity = 0.12f;

    static void register_module(flecs::world& ecs);
    static flecs::entity spawn_player_fly(flecs::world& ecs, const glm::vec3& position = {0.f, 8.f, 0.f});

    static void update_from_input(CameraComponent& camera, const Input& input, float fly_speed);
    static void build_render_snapshot(
        const CameraComponent& camera,
        const glm::vec3& render_origin,
        float aspect_ratio,
        WorldRenderSnapshot& snapshot,
        std::uint64_t frame_index);
};

} // namespace engine
