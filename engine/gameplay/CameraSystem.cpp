#include "engine/gameplay/CameraSystem.hpp"

#include <algorithm>

namespace engine {

void CameraSystem::register_module(flecs::world& ecs) {
    ecs.component<CameraComponent>();
}

flecs::entity CameraSystem::spawn_player_fly(flecs::world& ecs, const glm::vec3& position) {
    CameraComponent camera_component{};
    camera_component.camera.position = position;
    return ecs.entity("PlayerFly").set<CameraComponent>(camera_component);
}

void CameraSystem::update_look_from_input(CameraComponent& camera_component, const Input& input) {
    Camera& camera = camera_component.camera;
    camera.yaw += input.mouse_delta_x() * kMouseSensitivity;
    camera.pitch -= input.mouse_delta_y() * kMouseSensitivity;
    camera.pitch = std::clamp(camera.pitch, -89.f, 89.f);
}

void CameraSystem::update_from_input(CameraComponent& camera_component, const Input& input, float fly_speed) {
    Camera& camera = camera_component.camera;
    const float delta_time = input.delta_time();

    update_look_from_input(camera_component, input);

    glm::vec3 move_dir{0.f};
    if (input.move_forward()) {
        move_dir += camera.forward();
    }
    if (input.move_back()) {
        move_dir -= camera.forward();
    }
    if (input.move_right()) {
        move_dir += camera.right();
    }
    if (input.move_left()) {
        move_dir -= camera.right();
    }
    if (input.move_up()) {
        move_dir.y += 1.f;
    }
    if (input.move_down()) {
        move_dir.y -= 1.f;
    }

    if (glm::length(move_dir) > 0.f) {
        camera.position += glm::normalize(move_dir) * fly_speed * delta_time;
    }
}

void CameraSystem::build_render_snapshot(
    const CameraComponent& camera_component,
    const glm::vec3& render_origin,
    float aspect_ratio,
    WorldRenderSnapshot& snapshot,
    std::uint64_t frame_index) {
    const Camera& camera = camera_component.camera;

    snapshot.frame_index = frame_index;
    snapshot.render_origin = render_origin;
    snapshot.view = camera.view_matrix();
    snapshot.proj = camera.projection_matrix(aspect_ratio);
}

} // namespace engine
