#pragma once

namespace engine {

struct VoxelMovementConfig {
    float step_height = 0.6f;
    float skin_width = 0.03f;
    int max_solver_iterations = 4;
};

} // namespace engine
