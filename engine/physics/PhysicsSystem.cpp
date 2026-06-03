#include "engine/physics/PhysicsSystem.hpp"

#include <spdlog/spdlog.h>

#if defined(ENGINE_HAS_JOLT)
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#endif

namespace engine {

bool PhysicsSystem::init() {
#if defined(ENGINE_HAS_JOLT)
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    active_ = true;
    SPDLOG_INFO(
        "PhysicsSystem: Jolt minimal init (sensor/layer matrix stub; no terrain mesh collision)");
    return true;
#else
    active_ = true;
    SPDLOG_WARN("PhysicsSystem: BLOCKED — Jolt not linked; voxel capsule only");
    return true;
#endif
}

void PhysicsSystem::shutdown() {
#if defined(ENGINE_HAS_JOLT)
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
#endif
    active_ = false;
}

} // namespace engine
