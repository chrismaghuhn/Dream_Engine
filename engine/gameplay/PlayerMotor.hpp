#pragma once

#include "engine/gameplay/PlayerMotorConfig.hpp"
#include "engine/gameplay/VoxelMovementConfig.hpp"
#include "engine/physics/VoxelCapsuleResolver.hpp"
#include "engine/platform/Input.hpp"
#include "engine/world/ChunkStore.hpp"

#include <glm/glm.hpp>

namespace engine {

struct Camera;

struct PlayerMotorState {
    glm::vec3 velocity{0.f};
    Capsule capsule{};
    bool on_ground = false;
};

class PlayerMotor {
public:
    static constexpr float kDefaultRadius = 0.3f;
    static constexpr float kDefaultHalfHeight = 0.6f;
    static constexpr float kEyeHeight = 1.62f;

    PlayerMotorState& state() { return state_; }
    [[nodiscard]] const PlayerMotorState& state() const { return state_; }

    void sync_capsule_from_camera(const glm::vec3& camera_position);
    void apply_camera_to_capsule(Camera& camera) const;

    void tick_walk(
        Camera& camera,
        const Input& input,
        float fixed_dt,
        ChunkStore& store,
        const PlayerMotorConfig& motor,
        const VoxelMovementConfig& movement);

private:
    PlayerMotorState state_{};
};

} // namespace engine
