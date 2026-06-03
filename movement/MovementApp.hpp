#pragma once

#include "engine/movement/core/ArenaLoader.hpp"
#include "engine/movement/core/PersistentId.hpp"
#include "engine/movement/core/CameraSystem.hpp"
#include "engine/movement/core/CollisionWorld.hpp"
#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/InputSnapshot.hpp"
#include "engine/movement/core/MovementWorld.hpp"
#include "engine/movement/core/PlayerMovement.hpp"
#include "engine/movement/core/SaveService.hpp"
#include "engine/movement/debug_render/DebugDrawPass.hpp"
#include "engine/movement/debug_render/ImGuiOverlay.hpp"

#include "engine/character/core/AnimationController.hpp"
#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterComponents.hpp"
#include "engine/character/core/CombatController.hpp"
#include "engine/character/core/HitReactSystem.hpp"
#include "engine/character/render/CharacterPass.hpp"

#include "engine/core/SimClock.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/render/Renderer.hpp"

#include <string>

namespace engine::movement {

// Standalone third-person movement prototype. Owns the platform window, the
// renderer (with debug-line + ImGui pass extensions), the component-store world,
// and the fixed-step simulation loop.
class MovementApp {
public:
    bool startup();
    void run();
    void shutdown();

private:
    InputSnapshot poll_input();
    void build_debug_geometry(const glm::vec3& render_pos, float render_yaw);
    void save_player();
    void load_player();

    Platform platform_;
    Renderer renderer_;
    DebugDrawPass debug_pass_;
    ImGuiOverlay overlay_;
    engine::character::CharacterPass character_pass_;

    MovementWorld world_;
    CollisionWorld collision_;
    SimClock sim_clock_;
    MovementTuning tuning_;

    engine::character::CharacterAsset player_asset_;
    engine::character::AnimationState player_anim_;
    engine::character::CombatController player_combat_;
    engine::character::AttackTable attack_table_;
    int player_char_handle_ = -1;

    bool attack_light_down_   = false; bool attack_light_latch_   = false;
    bool attack_heavy_down_   = false; bool attack_heavy_latch_   = false;
    bool attack_kick_down_    = false; bool attack_kick_latch_    = false;
    bool attack_special_down_ = false; bool attack_special_latch_ = false;

    // Training dummy.
    engine::character::CharacterAsset dummy_asset_;
    engine::character::AnimationState dummy_anim_;
    engine::character::HitReact dummy_react_;
    EntityId dummy_entity_{};
    int dummy_char_handle_ = -1;

    std::string arena_id_;
    EntityId player_{};
    CapsuleCollider player_capsule_;
    MoveDebug debug_;

    double last_time_ = 0.0;
    double last_cursor_x_ = 0.0;
    double last_cursor_y_ = 0.0;
    bool first_mouse_ = true;
    bool cursor_captured_ = true;

    bool jump_down_ = false;
    bool jump_latch_ = false;
    bool save_down_ = false;
    bool load_down_ = false;
    bool depth_down_ = false;
    bool toggle_cursor_down_ = false;

    int sim_steps_last_frame_ = 0;
    float fps_ = 0.f;
    bool initialized_ = false;
};

} // namespace engine::movement
