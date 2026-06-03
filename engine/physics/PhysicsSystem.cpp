#include "engine/physics/PhysicsSystem.hpp"

#include "engine/physics/ObjectLayerMatrix.hpp"

#include <spdlog/spdlog.h>

#if defined(ENGINE_HAS_JOLT)
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <memory>
#endif

namespace engine {

#if defined(ENGINE_HAS_JOLT)
namespace {

static constexpr JPH::ObjectLayer kJoltStatic =
    static_cast<JPH::ObjectLayer>(static_cast<std::uint8_t>(ObjectLayer::Static));
static constexpr JPH::ObjectLayer kJoltPlayer =
    static_cast<JPH::ObjectLayer>(static_cast<std::uint8_t>(ObjectLayer::Player));
static constexpr JPH::ObjectLayer kJoltDebris =
    static_cast<JPH::ObjectLayer>(static_cast<std::uint8_t>(ObjectLayer::Debris));
static constexpr JPH::ObjectLayer kJoltSensor =
    static_cast<JPH::ObjectLayer>(static_cast<std::uint8_t>(ObjectLayer::Sensor));

static constexpr JPH::BroadPhaseLayer kBroadStatic(0);
static constexpr JPH::BroadPhaseLayer kBroadPlayer(1);
static constexpr JPH::BroadPhaseLayer kBroadDebris(2);
static constexpr JPH::BroadPhaseLayer kBroadSensor(3);
static constexpr std::uint32_t kBroadPhaseLayerCount = 4;

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerInterfaceImpl() {
        m_object_to_broad_phase[kJoltStatic] = kBroadStatic;
        m_object_to_broad_phase[kJoltPlayer] = kBroadPlayer;
        m_object_to_broad_phase[kJoltDebris] = kBroadDebris;
        m_object_to_broad_phase[kJoltSensor] = kBroadSensor;
    }

    std::uint32_t GetNumBroadPhaseLayers() const override { return kBroadPhaseLayerCount; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer layer) const override {
        return m_object_to_broad_phase[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(const JPH::BroadPhaseLayer layer) const override {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(layer)) {
        case static_cast<JPH::BroadPhaseLayer::Type>(kBroadStatic):
            return "Static";
        case static_cast<JPH::BroadPhaseLayer::Type>(kBroadPlayer):
            return "Player";
        case static_cast<JPH::BroadPhaseLayer::Type>(kBroadDebris):
            return "Debris";
        case static_cast<JPH::BroadPhaseLayer::Type>(kBroadSensor):
            return "Sensor";
        default:
            return "Invalid";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_object_to_broad_phase[static_cast<std::size_t>(ObjectLayer::Count)]{};
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(const JPH::ObjectLayer layer,
                       const JPH::BroadPhaseLayer broad_phase_layer) const override {
        switch (layer) {
        case kJoltStatic:
            return broad_phase_layer == kBroadPlayer || broad_phase_layer == kBroadDebris;
        case kJoltPlayer:
            return broad_phase_layer == kBroadStatic || broad_phase_layer == kBroadPlayer ||
                   broad_phase_layer == kBroadSensor;
        case kJoltDebris:
            return broad_phase_layer == kBroadStatic;
        case kJoltSensor:
            return broad_phase_layer == kBroadPlayer;
        default:
            return false;
        }
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(const JPH::ObjectLayer layer1, const JPH::ObjectLayer layer2) const override {
        return can_collide(
            static_cast<ObjectLayer>(layer1),
            static_cast<ObjectLayer>(layer2));
    }
};

struct JoltPhysicsState {
    std::unique_ptr<JPH::PhysicsSystem> system;
    BroadPhaseLayerInterfaceImpl broad_phase_layer_interface;
    ObjectVsBroadPhaseLayerFilterImpl object_vs_broad_phase_layer_filter;
    ObjectLayerPairFilterImpl object_layer_pair_filter;
};

JoltPhysicsState* g_jolt_state = nullptr;

} // namespace
#endif

bool PhysicsSystem::init() {
#if defined(ENGINE_HAS_JOLT)
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    auto state = std::make_unique<JoltPhysicsState>();
    state->system = std::make_unique<JPH::PhysicsSystem>();
    state->system->Init(
        1024,
        0,
        65536,
        10240,
        state->broad_phase_layer_interface,
        state->object_vs_broad_phase_layer_filter,
        state->object_layer_pair_filter);
    g_jolt_state = state.release();

    active_ = true;
    SPDLOG_INFO(
        "PhysicsSystem: Jolt init with object layer matrix (Static/Player/Debris/Sensor); "
        "terrain mesh collision via CollisionRemeshQueue pending M11");
    return true;
#else
    active_ = true;
    SPDLOG_WARN("PhysicsSystem: BLOCKED — Jolt not linked; voxel capsule only");
    return true;
#endif
}

void PhysicsSystem::shutdown() {
#if defined(ENGINE_HAS_JOLT)
    delete g_jolt_state;
    g_jolt_state = nullptr;
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
#endif
    active_ = false;
}

DebrisBodyHandle PhysicsSystem::create_debris_box(const glm::vec3& center, const float half_extent) {
#if defined(ENGINE_HAS_JOLT)
    if (g_jolt_state == nullptr || !active_) {
        return {};
    }

    const JPH::BoxShapeSettings box_settings(JPH::Vec3(half_extent, half_extent, half_extent));
    const JPH::ShapeSettings::ShapeResult shape_result = box_settings.Create();
    if (!shape_result.IsValid()) {
        return {};
    }

    const JPH::BodyCreationSettings settings(
        shape_result.Get(),
        JPH::RVec3(center.x, center.y, center.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        kJoltDebris);

    JPH::BodyInterface& body_interface = g_jolt_state->system->GetBodyInterface();
    JPH::Body* body = body_interface.CreateBody(settings);
    if (body == nullptr) {
        return {};
    }

    body_interface.AddBody(body->GetID(), JPH::EActivation::Activate);
    const JPH::BodyID id = body->GetID();
    return DebrisBodyHandle{
        .valid = true,
        .body_index = id.GetIndex(),
        .body_sequence = id.GetSequenceNumber(),
    };
#else
    (void)center;
    (void)half_extent;
    return {};
#endif
}

void PhysicsSystem::destroy_debris_body(DebrisBodyHandle& handle) {
#if defined(ENGINE_HAS_JOLT)
    if (!handle.valid || g_jolt_state == nullptr) {
        handle = {};
        return;
    }

    const JPH::BodyID id(handle.body_index, handle.body_sequence);
    JPH::BodyInterface& body_interface = g_jolt_state->system->GetBodyInterface();
    body_interface.RemoveBody(id);
    body_interface.DestroyBody(id);
#endif
    handle = {};
}

ObjectLayer PhysicsSystem::debris_body_layer(const DebrisBodyHandle& handle) const {
#if defined(ENGINE_HAS_JOLT)
    if (!handle.valid || g_jolt_state == nullptr) {
        return ObjectLayer::Count;
    }

    const JPH::BodyID id(handle.body_index, handle.body_sequence);
    JPH::BodyInterface& body_interface = g_jolt_state->system->GetBodyInterface();
    if (!body_interface.IsAdded(id)) {
        return ObjectLayer::Count;
    }

    return static_cast<ObjectLayer>(body_interface.GetObjectLayer(id));
#else
    (void)handle;
    return ObjectLayer::Count;
#endif
}

} // namespace engine
