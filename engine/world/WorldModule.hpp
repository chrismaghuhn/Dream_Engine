#pragma once

#include <flecs.h>

namespace engine {

struct WorldModule {
    explicit WorldModule(flecs::world& ecs);
};

} // namespace engine
