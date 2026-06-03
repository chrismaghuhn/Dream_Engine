#include "movement/MovementApp.hpp"

#include "engine/movement/core/Interpolation.hpp"
#include "engine/render/WorldRenderSnapshot.hpp"

#include "engine/character/core/AnimationController.hpp"
#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterCatalog.hpp"
#include "engine/character/core/CombatController.hpp"
#include "engine/character/core/HitDetection.hpp"
#include "engine/character/core/HitReactSystem.hpp"
#include "engine/movement/core/Components.hpp"

#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

namespace engine::movement {

namespace {
constexpr float kMouseSensitivity = 0.0025f;
constexpr float kFovYDeg = 60.f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 500.f;

glm::mat4 projection_matrix(float aspect_ratio) {
    glm::mat4 proj = glm::perspective(glm::radians(kFovYDeg), aspect_ratio, kNearPlane, kFarPlane);
    proj[1][1] *= -1.f; // Vulkan NDC Y flip (matches engine Camera)
    return proj;
}
} // namespace

bool MovementApp::startup() {
    if (!platform_.init(1280, 720, "Movement Foundation v1")) {
        SPDLOG_ERROR("MovementApp: platform init failed");
        return false;
    }

    MemoryBudget budget;
    budget.gpu_mesh_vram = 32ull * 1024 * 1024;
    budget.chunk_mesh_cpu_ram = 32ull * 1024 * 1024;
    if (!renderer_.init(platform_, budget)) {
        SPDLOG_ERROR("MovementApp: renderer init failed");
        return false;
    }

    overlay_.set_window(platform_.window());
    overlay_.set_renderer(&renderer_);
    renderer_.register_extension(pass_insertion::kBeforeImgui, &debug_pass_);
    renderer_.register_extension(pass_insertion::kBeforeImgui, &character_pass_);
    renderer_.register_extension(pass_insertion::kBeforeImgui, &overlay_);

    // Load the arena into the component-store world.
    const std::filesystem::path arena_path =
        std::filesystem::path(ENGINE_SOURCE_DIR) / "assets" / "movement" / "combat_test.arena";
    try {
        const ArenaLoadResult loaded = load_arena_file(arena_path, world_);
        arena_id_ = loaded.arena_id;
        player_ = loaded.player;
    } catch (const ParseException& ex) {
        SPDLOG_ERROR("MovementApp: failed to load arena: {}", ex.what());
        return false;
    }
    if (player_.is_null()) {
        SPDLOG_ERROR("MovementApp: arena has no player entity");
        return false;
    }

    // Load player character asset (mesh + locomotion clips) and register with CharacterPass.
    try {
        player_asset_ = engine::character::CharacterCatalog::load_player_set();
        player_char_handle_ = character_pass_.add_character(player_asset_);
        player_anim_.active_clip = "Walk";
        player_anim_.looping     = true;

        // Setup combo chain.
        player_combat_.combo_ids = {"high_kick", "elbow_strike", "counterstrike"};

        SPDLOG_INFO("MovementApp: player character loaded ({} clips)",
                    player_asset_.clips.size());
    } catch (const std::exception& ex) {
        SPDLOG_WARN("MovementApp: player character load failed: {}", ex.what());
    }

    // Find training dummy entity in the world.
    {
        const PersistentId dummy_pid = PersistentId::make(arena_id_, "training_dummy");
        dummy_entity_ = world_.find(dummy_pid);
    }

    // Load dummy character asset and register with CharacterPass.
    try {
        dummy_asset_ = engine::character::CharacterCatalog::load_dummy_set();
        dummy_char_handle_ = character_pass_.add_character(dummy_asset_);
        dummy_anim_.active_clip = "Hit_Reaction_1";
        dummy_anim_.time_seconds = 0.f;
        dummy_anim_.looping = true; // idle: hold first frame by looping a short clip
        dummy_anim_.speed = 0.f;   // freeze on frame 0 until hit
        dummy_react_.knockback_distance = 0.3f;
        dummy_react_.knockback_duration = 0.25f;
        dummy_react_.hit_clip = "Hit_Reaction_1";
        SPDLOG_INFO("MovementApp: dummy character loaded ({} clips)", dummy_asset_.clips.size());
    } catch (const std::exception& ex) {
        SPDLOG_WARN("MovementApp: dummy character load failed: {}", ex.what());
    }

    // Load attack data table.
    const std::string attack_file =
        std::string(ENGINE_SOURCE_DIR) + "/assets/character/combat_attacks.txt";
    try {
        attack_table_ = engine::character::AttackData::load(attack_file);
        SPDLOG_INFO("MovementApp: loaded {} attack definitions", attack_table_.size());
    } catch (const std::exception& ex) {
        SPDLOG_WARN("MovementApp: attack table load failed: {}", ex.what());
    }

    // Static collision is built once from the arena's box/ground colliders.
    build_collision_world(world_, collision_);

    // Player capsule derived from its collider component.
    if (const Collider* col = world_.colliders().get(player_); col != nullptr &&
                                                               col->shape == ColliderShape::Capsule) {
        player_capsule_ = CapsuleCollider::from_dimensions(col->radius, col->height);
    } else {
        player_capsule_ = CapsuleCollider::from_dimensions(0.4f, 1.8f);
    }

    // Apply save overrides if a save file exists.
    const std::filesystem::path save_path = SaveService::default_save_path();
    std::error_code ec;
    if (std::filesystem::exists(save_path, ec)) {
        try {
            SaveService::load(save_path, world_, arena_id_);
            SPDLOG_INFO("MovementApp: applied save overrides");
        } catch (const ParseException& ex) {
            SPDLOG_WARN("MovementApp: save ignored: {}", ex.what());
        }
    }

    glfwSetInputMode(platform_.window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    cursor_captured_ = true;
    last_time_ = glfwGetTime();

    initialized_ = true;
    SPDLOG_INFO("MovementApp ready: arena '{}', overlay={}", arena_id_, overlay_.initialized());
    return true;
}

InputSnapshot MovementApp::poll_input() {
    InputSnapshot snap;
    GLFWwindow* window = platform_.window();

    snap.move_forward = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    snap.move_back = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    snap.move_left = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    snap.move_right = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    snap.sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                  glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    const bool jump_now = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (jump_now && !jump_down_) {
        jump_latch_ = true;  // latch on rising edge
    }
    if (!jump_now) {
        jump_latch_ = false; // cancel if key released before a jump fires
    }
    jump_down_ = jump_now;
    snap.jump_pressed = jump_latch_;

    snap.mouse_delta = glm::vec2(0.f);
    if (cursor_captured_) {
        double cx = 0.0;
        double cy = 0.0;
        glfwGetCursorPos(window, &cx, &cy);
        if (first_mouse_) {
            last_cursor_x_ = cx;
            last_cursor_y_ = cy;
            first_mouse_ = false;
        }
        snap.mouse_delta = glm::vec2(static_cast<float>(cx - last_cursor_x_),
                                     static_cast<float>(cy - last_cursor_y_));
        last_cursor_x_ = cx;
        last_cursor_y_ = cy;
    }

    // LMB edge-trigger for attack (same latch pattern as jump).
    const bool attack_now = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (attack_now && !attack_down_) {
        snap.attack_pressed = true;
    }
    attack_down_ = attack_now;

    const bool save_now = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
    if (save_now && !save_down_) {
        save_player();
    }
    save_down_ = save_now;

    const bool load_now = glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS;
    if (load_now && !load_down_) {
        load_player();
    }
    load_down_ = load_now;

    const bool depth_now = glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS;
    if (depth_now && !depth_down_) {
        debug_pass_.set_depth_test(!debug_pass_.depth_test());
    }
    depth_down_ = depth_now;

    const bool toggle_cursor_now = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS;
    if (toggle_cursor_now && !toggle_cursor_down_) {
        cursor_captured_ = !cursor_captured_;
        glfwSetInputMode(window, GLFW_CURSOR,
                         cursor_captured_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        first_mouse_ = true;
    }
    toggle_cursor_down_ = toggle_cursor_now;

    return snap;
}

void MovementApp::build_debug_geometry(const glm::vec3& render_pos, float render_yaw) {
    debug_pass_.begin_frame();

    const Transform* tf = world_.transforms().get(player_);
    const glm::vec3 sim_pos = tf != nullptr ? tf->position : render_pos;

    // Capsule at SIMULATION state (spec 13: collision debug shows sim truth).
    const std::uint32_t capsule_color =
        debug_.grounded ? DebugDrawPass::rgba(80, 220, 120) : DebugDrawPass::rgba(220, 160, 60);
    debug_pass_.push_capsule(sim_pos, player_capsule_.radius, player_capsule_.half_height,
                             capsule_color);

    // Faint render-interpolated capsule for comparison.
    if (glm::distance(sim_pos, render_pos) > 1e-3f) {
        debug_pass_.push_capsule(render_pos, player_capsule_.radius, player_capsule_.half_height,
                                 DebugDrawPass::rgba(90, 90, 110, 160));
    }

    // Static colliders.
    for (const BoxCollider& box : collision_.boxes()) {
        debug_pass_.push_box(box.center, box.half_extents, DebugDrawPass::rgba(120, 140, 200));
    }

    // Ground grid.
    if (collision_.ground_enabled()) {
        const std::uint32_t grid_color = DebugDrawPass::rgba(70, 70, 90);
        const int half = 10;
        const float gy = collision_.ground_y() + 0.01f;
        const float ox = std::round(sim_pos.x);
        const float oz = std::round(sim_pos.z);
        for (int i = -half; i <= half; ++i) {
            debug_pass_.push_line(glm::vec3(ox + i, gy, oz - half),
                                  glm::vec3(ox + i, gy, oz + half), grid_color);
            debug_pass_.push_line(glm::vec3(ox - half, gy, oz + i),
                                  glm::vec3(ox + half, gy, oz + i), grid_color);
        }
    }

    // Contact points (red) and their normals.
    const std::uint32_t contact_color = DebugDrawPass::rgba(240, 60, 60);
    for (std::size_t i = 0; i < debug_.contact_points.size(); ++i) {
        debug_pass_.push_cross(debug_.contact_points[i], 0.25f, contact_color);
        if (i < debug_.contact_normals.size()) {
            debug_pass_.push_arrow(debug_.contact_points[i], debug_.contact_normals[i], 0.5f,
                                   contact_color);
        }
    }

    // Downward ground probe (yellow).
    if (debug_.probe_hit) {
        debug_pass_.push_cross(debug_.probe_point, 0.3f, DebugDrawPass::rgba(240, 220, 60));
    }

    // Player facing direction (movement-driven yaw), at sim state.
    const glm::vec3 facing(std::sin(render_yaw), 0.f, std::cos(render_yaw));
    debug_pass_.push_arrow(sim_pos + glm::vec3(0.f, player_capsule_.half_height, 0.f), facing, 1.5f,
                           DebugDrawPass::rgba(240, 80, 80));

    // Dummy collider (blue box).
    if (world_.is_alive(dummy_entity_)) {
        if (const Transform* dtf = world_.transforms().get(dummy_entity_)) {
            if (const Collider* dcol = world_.colliders().get(dummy_entity_)) {
                debug_pass_.push_box(dtf->position, dcol->half_extents,
                                     DebugDrawPass::rgba(80, 120, 240));
            }
        }
    }

    // Hit capsule during attacking hit window (red).
    if (player_combat_.phase == engine::character::CombatPhase::Attacking &&
        !attack_table_.empty()) {
        const int ci = player_combat_.combo_index;
        if (ci >= 0 && ci < static_cast<int>(player_combat_.combo_ids.size())) {
            const std::string& atk_id = player_combat_.combo_ids[
                static_cast<std::size_t>(ci)];
            auto ait = attack_table_.find(atk_id);
            if (ait != attack_table_.end()) {
                const engine::character::AttackDef& def = ait->second;
                // Compute normalized time (same formula as try_hit_in_window).
                const float elapsed   = player_anim_.time_seconds;
                const float remaining = player_combat_.clip_remaining;
                const float orig_dur  = elapsed + (remaining > 0.f ? remaining : 0.f);
                const float norm_t    = (orig_dur > 1e-5f) ? elapsed / orig_dur : 1.f;
                const bool in_window  = norm_t >= def.hit_start && norm_t <= def.hit_end;

                const float yaw = player_combat_.attack_yaw;
                const glm::vec3 fwd(std::sin(yaw), 0.f, std::cos(yaw));
                const glm::vec3 hit_center = sim_pos + fwd * def.range;
                const std::uint32_t hit_color = in_window
                    ? DebugDrawPass::rgba(255, 60, 60)
                    : DebugDrawPass::rgba(255, 180, 60, 128);
                debug_pass_.push_capsule(hit_center, def.radius, def.radius * 0.5f, hit_color);
            }
        }
    }
}

void MovementApp::save_player() {
    try {
        SaveService::save(SaveService::default_save_path(), world_, "slot_01", arena_id_);
        SPDLOG_INFO("Saved player state");
    } catch (const ParseException& ex) {
        SPDLOG_ERROR("Save failed: {}", ex.what());
    }
}

void MovementApp::load_player() {
    try {
        SaveService::load(SaveService::default_save_path(), world_, arena_id_);
        SPDLOG_INFO("Loaded player state");
    } catch (const ParseException& ex) {
        SPDLOG_WARN("Load failed: {}", ex.what());
    }
}

void MovementApp::run() {
    if (!initialized_) {
        return;
    }

    while (!platform_.should_close()) {
        platform_.poll();

        const double now = glfwGetTime();
        double frame_dt = now - last_time_;
        last_time_ = now;
        frame_dt = std::min(frame_dt, 0.25);
        if (frame_dt > 0.0) {
            fps_ = fps_ * 0.9f + static_cast<float>(1.0 / frame_dt) * 0.1f;
        }

        InputSnapshot input = poll_input();

        CameraRig* rig = world_.camera_rigs().get(player_);
        Transform* tf = world_.transforms().get(player_);
        PlayerController* pc = world_.controllers().get(player_);
        if (rig == nullptr || tf == nullptr || pc == nullptr) {
            break;
        }

        // Camera rotates once per frame from accumulated mouse delta.
        camera::update(*rig, input.mouse_delta, kMouseSensitivity, input.scroll_delta);

        sim_clock_.advance(frame_dt);
        sim_steps_last_frame_ = 0;
        sim_clock_.step([&] {
            tf->sync_previous(); // previous = current before each fixed step

            // Combat FSM tick (before movement — may freeze player_tick).
            const bool is_attacking =
                player_combat_.phase == engine::character::CombatPhase::Attacking ||
                player_combat_.phase == engine::character::CombatPhase::Recovery;

            if (player_char_handle_ >= 0 && !attack_table_.empty()) {
                engine::character::combat_tick(
                    player_combat_, *tf, player_anim_, input,
                    attack_table_, player_asset_.clips,
                    static_cast<float>(SimClock::fixed_dt));
            }

            // Freeze horizontal movement while attacking (spec 4.4).
            if (is_attacking) {
                // Zero out move input so player_tick doesn't move the character.
                InputSnapshot frozen_input = input;
                frozen_input.move_forward = false;
                frozen_input.move_back    = false;
                frozen_input.move_left    = false;
                frozen_input.move_right   = false;
                frozen_input.jump_pressed = false;
                debug_ = player_tick(*tf, *pc, player_capsule_, collision_,
                                     frozen_input, rig->yaw,
                                     static_cast<float>(SimClock::fixed_dt), tuning_);
            } else {
                debug_ = player_tick(*tf, *pc, player_capsule_, collision_, input,
                                     rig->yaw, static_cast<float>(SimClock::fixed_dt),
                                     tuning_);
            }

            // Select and advance locomotion animation (only when not in combat clip).
            if (player_char_handle_ >= 0 &&
                player_combat_.phase == engine::character::CombatPhase::Idle) {
                const float horiz_speed = glm::length(
                    glm::vec2(pc->velocity.x, pc->velocity.z));
                const std::string desired =
                    engine::character::AnimationController::select_locomotion(
                        horiz_speed, pc->grounded);
                if (desired != player_anim_.active_clip) {
                    player_anim_.active_clip  = desired;
                    player_anim_.time_seconds = 0.f;
                    player_anim_.looping      = true;
                }
            }

            // Advance player animation time.
            if (player_char_handle_ >= 0) {
                const engine::character::AnimClip* clip = nullptr;
                for (const auto& c : player_asset_.clips) {
                    if (c.name == player_anim_.active_clip) { clip = &c; break; }
                }
                engine::character::AnimationController::tick(
                    player_anim_, clip, static_cast<float>(SimClock::fixed_dt));
            }

            // Hit detection: test hit window against training dummy.
            if (player_combat_.phase == engine::character::CombatPhase::Attacking &&
                dummy_char_handle_ >= 0 && world_.is_alive(dummy_entity_) &&
                !attack_table_.empty()) {
                const int combo_i = player_combat_.combo_index;
                if (combo_i >= 0 && combo_i < static_cast<int>(player_combat_.combo_ids.size())) {
                    const std::string& atk_id = player_combat_.combo_ids[
                        static_cast<std::size_t>(combo_i)];
                    auto ait = attack_table_.find(atk_id);
                    if (ait != attack_table_.end()) {
                        Transform* dummy_tf = world_.transforms().get(dummy_entity_);
                        Collider*  dummy_col = world_.colliders().get(dummy_entity_);
                        if (dummy_tf && dummy_col) {
                            const bool hit = engine::character::try_hit_in_window(
                                player_combat_, player_anim_, ait->second,
                                *tf, *dummy_tf, *dummy_col);
                            if (hit) {
                                const glm::vec3 dir = dummy_tf->position - tf->position;
                                const glm::vec3 dir_norm =
                                    glm::length(dir) > 1e-5f
                                        ? glm::normalize(dir)
                                        : glm::vec3(0.f, 0.f, 1.f);
                                engine::character::trigger_hit_react(
                                    dummy_react_, *dummy_tf, dir_norm, dummy_anim_);
                                SPDLOG_INFO("Hit! combo[{}] attack '{}'",
                                            combo_i, atk_id);
                            }
                        }
                    }
                }
            }

            // HitReact tick on dummy.
            if (dummy_char_handle_ >= 0 && world_.is_alive(dummy_entity_)) {
                Transform* dummy_tf = world_.transforms().get(dummy_entity_);
                if (dummy_tf) {
                    engine::character::hit_react_tick(
                        dummy_react_, *dummy_tf,
                        static_cast<float>(SimClock::fixed_dt));

                    // Advance dummy animation.
                    const engine::character::AnimClip* dclip = nullptr;
                    for (const auto& c : dummy_asset_.clips) {
                        if (c.name == dummy_anim_.active_clip) { dclip = &c; break; }
                    }
                    engine::character::AnimationController::tick(
                        dummy_anim_, dclip, static_cast<float>(SimClock::fixed_dt));

                    // After hit animation ends, freeze on last frame.
                    if (!dummy_anim_.looping && dclip &&
                        dummy_anim_.time_seconds >= dclip->duration_seconds) {
                        dummy_anim_.time_seconds = dclip->duration_seconds;
                        dummy_anim_.speed = 0.f;
                    }
                }
            }

            ++sim_steps_last_frame_;
        });
        // player_tick clears input.jump_pressed when the jump is consumed. Mirror
        // that back into jump_latch_ so the request is not repeated next frame.
        if (!input.jump_pressed) {
            jump_latch_ = false;
        }

        const float alpha = static_cast<float>(sim_clock_.alpha());
        const glm::vec3 render_pos = lerp_position(tf->previous_position, tf->position, alpha);
        const float render_yaw = lerp_angle(tf->previous_yaw, tf->yaw, alpha);

        build_debug_geometry(render_pos, render_yaw);

        if (renderer_.device_lost()) {
            renderer_.recover_if_device_lost();
            continue;
        }

        const std::uint32_t slot = renderer_.snapshot_ring().pick_write_slot();
        if (renderer_.snapshot_ring().consume_pick_device_lost()) {
            renderer_.note_device_lost();
            renderer_.recover_if_device_lost();
            continue;
        }

        WorldRenderSnapshot& snap = renderer_.snapshot_ring().snapshot(slot);
        snap.opaque_sections.clear();
        snap.water_sections.clear();
        snap.render_origin = glm::vec3(0.f);
        snap.view = camera::view_matrix(*rig, render_pos);
        snap.proj = projection_matrix(renderer_.aspect_ratio());

        // Upload bone matrices + model matrix for the player character.
        if (player_char_handle_ >= 0) {
            const engine::character::AnimClip* clip = nullptr;
            for (const auto& c : player_asset_.clips) {
                if (c.name == player_anim_.active_clip) {
                    clip = &c;
                    break;
                }
            }
            const std::vector<glm::mat4> bone_mats =
                engine::character::AnimationController::sample_bone_matrices(
                    player_anim_, clip, player_asset_.mesh);

            // Model matrix: interpolated position + yaw.
            glm::mat4 model = glm::mat4(1.f);
            model = glm::translate(model, render_pos);
            model = glm::rotate(model, render_yaw, glm::vec3(0.f, 1.f, 0.f));

            character_pass_.set_pose(player_char_handle_, slot, model, bone_mats);
        }

        // Dummy: upload bone matrices + current transform.
        if (dummy_char_handle_ >= 0 && world_.is_alive(dummy_entity_)) {
            const Transform* dummy_tf = world_.transforms().get(dummy_entity_);
            if (dummy_tf) {
                const engine::character::AnimClip* dclip = nullptr;
                for (const auto& c : dummy_asset_.clips) {
                    if (c.name == dummy_anim_.active_clip) { dclip = &c; break; }
                }
                const std::vector<glm::mat4> dummy_bones =
                    engine::character::AnimationController::sample_bone_matrices(
                        dummy_anim_, dclip, dummy_asset_.mesh);

                const float d_alpha = static_cast<float>(sim_clock_.alpha());
                const glm::vec3 d_render_pos = lerp_position(
                    dummy_tf->previous_position, dummy_tf->position, d_alpha);
                const float d_render_yaw = lerp_angle(
                    dummy_tf->previous_yaw, dummy_tf->yaw, d_alpha);

                glm::mat4 d_model = glm::mat4(1.f);
                d_model = glm::translate(d_model, d_render_pos);
                d_model = glm::rotate(d_model, d_render_yaw, glm::vec3(0.f, 1.f, 0.f));

                character_pass_.set_pose(dummy_char_handle_, slot, d_model, dummy_bones);
            }
        }

        const PersistentId* pid = world_.persistent_id(player_);

        MovementOverlayState overlay_state;
        overlay_state.grounded = pc->grounded;
        overlay_state.wall_contact = debug_.wall_contact;
        overlay_state.velocity = pc->velocity;
        overlay_state.position = tf->position;
        overlay_state.yaw = rig->yaw;
        overlay_state.pitch = rig->pitch;
        overlay_state.fps = fps_;
        overlay_state.depth_test = debug_pass_.depth_test();
        overlay_state.sim_steps_last_frame = sim_steps_last_frame_;
        overlay_state.accumulator_alpha = alpha;
        overlay_state.persistent_id = pid != nullptr ? pid->str().c_str() : "";

        // Combat state.
        using CP = engine::character::CombatPhase;
        overlay_state.combat_phase =
            player_combat_.phase == CP::Attacking ? "Attacking" :
            player_combat_.phase == CP::Recovery  ? "Recovery"  : "Idle";
        overlay_state.active_clip  = player_anim_.active_clip.c_str();
        overlay_state.combo_index  = player_combat_.combo_index;
        overlay_state.attack_yaw_deg =
            glm::degrees(player_combat_.attack_yaw);
        overlay_state.hit_consumed = player_combat_.hit_consumed;

        // Hit-window diagnostics for current attack.
        if (player_combat_.phase == CP::Attacking && !attack_table_.empty()) {
            const int ci = player_combat_.combo_index;
            if (ci >= 0 && ci < static_cast<int>(player_combat_.combo_ids.size())) {
                const std::string& atk_id = player_combat_.combo_ids[
                    static_cast<std::size_t>(ci)];
                auto ait = attack_table_.find(atk_id);
                if (ait != attack_table_.end()) {
                    const auto& def = ait->second;
                    overlay_state.hit_window_start = def.hit_start;
                    overlay_state.hit_window_end   = def.hit_end;
                    const float elapsed   = player_anim_.time_seconds;
                    const float remaining = player_combat_.clip_remaining;
                    const float orig_dur  = elapsed + (remaining > 0.f ? remaining : 0.f);
                    const float norm_t    = (orig_dur > 1e-5f) ? elapsed / orig_dur : 1.f;
                    overlay_state.normalized_clip_time = norm_t;
                    overlay_state.in_hit_window =
                        norm_t >= def.hit_start && norm_t <= def.hit_end;
                }
            }
        } else {
            overlay_state.hit_window_start = 0.f;
            overlay_state.hit_window_end   = 0.f;
            overlay_state.normalized_clip_time = 0.f;
            overlay_state.in_hit_window    = false;
        }
        overlay_.new_frame(overlay_state);

        renderer_.render_frame(slot);
    }
}

void MovementApp::shutdown() {
    if (renderer_.initialized()) {
        renderer_.shutdown();
    }
    platform_.shutdown();
    initialized_ = false;
}

} // namespace engine::movement
