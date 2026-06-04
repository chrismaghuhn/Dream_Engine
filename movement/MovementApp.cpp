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

const engine::character::AnimClip* find_clip(
    const engine::character::CharacterAsset& asset,
    const std::string& name) {
    for (const auto& clip : asset.clips) {
        if (clip.name == name) {
            return &clip;
        }
    }
    return nullptr;
}

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
        player_anim_.active_clip = "Idle";
        player_anim_.looping     = true;

        player_chains_[engine::character::kLightChain] = {
            "jab_left", "jab_right", "hook_left", "uppercut_right"};
        player_chains_[engine::character::kHeavyChain] = {
            "uppercut_left", "upper_hook_right", "knee_strike"};
        player_chains_[engine::character::kKickChain] = {
            "high_kick_step", "roundhouse", "spin_kick"};
        player_chains_[engine::character::kSpecialChain] = {
            "shield_push"};

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

    auto edge_latch = [](bool now, bool& down, bool& latch) {
        if (now && !down) latch = true;
        if (!now)         latch = false;
        down = now;
    };

    edge_latch(glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS,
               attack_light_down_,   attack_light_latch_);
    edge_latch(glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS,
               attack_heavy_down_,   attack_heavy_latch_);
    edge_latch(glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS,
               attack_kick_down_,    attack_kick_latch_);
    edge_latch(glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS,
               attack_special_down_, attack_special_latch_);

    snap.attack_light   = attack_light_latch_;
    snap.attack_heavy   = attack_heavy_latch_;
    snap.attack_kick    = attack_kick_latch_;
    snap.attack_special = attack_special_latch_;
    snap.dodge_pressed  = snap.jump_pressed; // Space doubles as dodge during combat

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

    // Hit capsule during combat. Orange outside the active hit window, red inside.
    if (player_combat_.phase != engine::character::CombatPhase::Idle &&
        !attack_table_.empty()) {
        const int ci = player_combat_.combo_index;
        if (ci >= 0 && ci < static_cast<int>(player_combat_.combo_ids.size())) {
            const std::string& atk_id = player_combat_.combo_ids[
                static_cast<std::size_t>(ci)];
            auto ait = attack_table_.find(atk_id);
            if (ait != attack_table_.end()) {
                const engine::character::AttackDef& def = ait->second;
                const engine::character::AnimClip* clip =
                    find_clip(player_asset_, def.clip);
                const float duration = clip != nullptr ? clip->duration_seconds : 1.f;
                const float norm_t =
                    duration > 1e-5f ? player_anim_.time_seconds / duration : 1.f;
                const bool in_window =
                    norm_t >= def.hit_start_norm && norm_t <= def.hit_end_norm;

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
            player_input_buffer_.tick();

            if (input.attack_light) {
                player_input_buffer_.push(engine::character::BufferedInput::Kind::Light);
                attack_light_latch_ = false;
                input.attack_light = false;
            }
            if (input.attack_heavy) {
                player_input_buffer_.push(engine::character::BufferedInput::Kind::Heavy);
                attack_heavy_latch_ = false;
                input.attack_heavy = false;
            }
            if (input.attack_kick) {
                player_input_buffer_.push(engine::character::BufferedInput::Kind::Kick);
                attack_kick_latch_ = false;
                input.attack_kick = false;
            }
            if (input.attack_special) {
                player_input_buffer_.push(engine::character::BufferedInput::Kind::Special);
                attack_special_latch_ = false;
                input.attack_special = false;
            }
            if (input.dodge_pressed &&
                player_combat_.phase != engine::character::CombatPhase::Idle) {
                player_input_buffer_.push(engine::character::BufferedInput::Kind::Dodge);
                input.dodge_pressed = false;
            }

            if (player_char_handle_ >= 0) {
                const engine::character::AnimClip* clip =
                    find_clip(player_asset_, player_anim_.active_clip);
                engine::character::AnimationController::tick(
                    player_anim_, clip, static_cast<float>(SimClock::fixed_dt));
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
                    engine::character::AnimationController::crossfade_to(
                        player_anim_, desired, 0.1f, true);
                }
            }

            if (player_char_handle_ >= 0 && !attack_table_.empty()) {
                engine::character::combat_tick(
                    player_combat_, *tf, player_anim_,
                    player_input_buffer_, attack_table_, player_asset_.clips,
                    player_chains_, static_cast<float>(SimClock::fixed_dt));
            }

            const bool is_combat_locked =
                player_combat_.phase != engine::character::CombatPhase::Idle;
            InputSnapshot move_input = input;
            if (is_combat_locked) {
                move_input.move_forward = false;
                move_input.move_back    = false;
                move_input.move_left    = false;
                move_input.move_right   = false;
                move_input.jump_pressed = false;
            }
            debug_ = player_tick(*tf, *pc, player_capsule_, collision_, move_input,
                                 rig->yaw, static_cast<float>(SimClock::fixed_dt),
                                 tuning_);

            // Hit detection: test hit window against training dummy.
            if (player_combat_.phase == engine::character::CombatPhase::Active &&
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
                            const engine::character::AnimClip* clip =
                                find_clip(player_asset_, ait->second.clip);
                            const float clip_duration =
                                clip != nullptr ? clip->duration_seconds : 1.f;
                            const bool hit = engine::character::try_hit_in_window(
                                player_combat_, player_anim_, ait->second, clip_duration,
                                *tf, *dummy_tf, *dummy_col);
                            if (hit) {
                                const glm::vec3 dir = dummy_tf->position - tf->position;
                                const glm::vec3 dir_norm =
                                    glm::length(dir) > 1e-5f
                                        ? glm::normalize(dir)
                                        : glm::vec3(0.f, 0.f, 1.f);
                                player_anim_.speed = 0.f;
                                engine::character::trigger_hit_react(
                                    dummy_react_, *dummy_tf, dir_norm, dummy_anim_,
                                    &player_combat_, &camera_shake_,
                                    ait->second.hitstop_frames);
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
                        static_cast<float>(SimClock::fixed_dt),
                        player_combat_.hitstop_active);

                    // Advance dummy animation.
                    const engine::character::AnimClip* dclip =
                        find_clip(dummy_asset_, dummy_anim_.active_clip);
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
        // Mirror consumed flags back into their latches.
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
        engine::character::tick_screenshake(camera_shake_, static_cast<float>(frame_dt));
        const glm::vec3 shake_offset =
            engine::character::screenshake_offset(camera_shake_);
        snap.view = camera::view_matrix(*rig, render_pos + shake_offset);
        snap.proj = projection_matrix(renderer_.aspect_ratio());

        // Upload bone matrices + model matrix for the player character.
        if (player_char_handle_ >= 0) {
            const engine::character::AnimClip* clip =
                find_clip(player_asset_, player_anim_.active_clip);
            const engine::character::AnimClip* blend_clip =
                find_clip(player_asset_, player_anim_.blend_clip);
            const std::vector<glm::mat4> bone_mats =
                engine::character::AnimationController::sample_bone_matrices(
                    player_anim_, clip, player_asset_.mesh, blend_clip);

            // Model matrix: position + yaw * GLB root transform (handles cm→m scale).
            glm::mat4 model = glm::mat4(1.f);
            model = glm::translate(model, render_pos);
            model = glm::rotate(model, render_yaw, glm::vec3(0.f, 1.f, 0.f));
            model = model * player_asset_.node_transform;

            character_pass_.set_pose(player_char_handle_, slot, model, bone_mats);
        }

        // Dummy: upload bone matrices + current transform.
        if (dummy_char_handle_ >= 0 && world_.is_alive(dummy_entity_)) {
            const Transform* dummy_tf = world_.transforms().get(dummy_entity_);
            if (dummy_tf) {
                const engine::character::AnimClip* dclip =
                    find_clip(dummy_asset_, dummy_anim_.active_clip);
                const engine::character::AnimClip* dblend_clip =
                    find_clip(dummy_asset_, dummy_anim_.blend_clip);
                const std::vector<glm::mat4> dummy_bones =
                    engine::character::AnimationController::sample_bone_matrices(
                        dummy_anim_, dclip, dummy_asset_.mesh, dblend_clip);

                const float d_alpha = static_cast<float>(sim_clock_.alpha());
                const glm::vec3 d_render_pos = lerp_position(
                    dummy_tf->previous_position, dummy_tf->position, d_alpha);
                const float d_render_yaw = lerp_angle(
                    dummy_tf->previous_yaw, dummy_tf->yaw, d_alpha);

                glm::mat4 d_model = glm::mat4(1.f);
                d_model = glm::translate(d_model, d_render_pos);
                d_model = glm::rotate(d_model, d_render_yaw, glm::vec3(0.f, 1.f, 0.f));
                d_model = d_model * dummy_asset_.node_transform;

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
            player_combat_.phase == CP::Startup     ? "Startup" :
            player_combat_.phase == CP::Active      ? "Active" :
            player_combat_.phase == CP::Recovery    ? "Recovery" :
            player_combat_.phase == CP::DodgeCancel ? "DodgeCancel" : "Idle";
        overlay_state.active_clip  = player_anim_.active_clip.c_str();
        overlay_state.combo_index  = player_combat_.combo_index;
        overlay_state.attack_yaw_deg =
            glm::degrees(player_combat_.attack_yaw);
        overlay_state.hit_consumed = player_combat_.hit_consumed;

        // Hit-window diagnostics for current attack.
        if (player_combat_.phase != CP::Idle && !attack_table_.empty()) {
            const int ci = player_combat_.combo_index;
            if (ci >= 0 && ci < static_cast<int>(player_combat_.combo_ids.size())) {
                const std::string& atk_id = player_combat_.combo_ids[
                    static_cast<std::size_t>(ci)];
                auto ait = attack_table_.find(atk_id);
                if (ait != attack_table_.end()) {
                    const auto& def = ait->second;
                    overlay_state.hit_window_start = def.hit_start_norm;
                    overlay_state.hit_window_end   = def.hit_end_norm;
                    const engine::character::AnimClip* clip =
                        find_clip(player_asset_, def.clip);
                    const float duration = clip != nullptr ? clip->duration_seconds : 1.f;
                    const float norm_t =
                        duration > 1e-5f ? player_anim_.time_seconds / duration : 1.f;
                    overlay_state.normalized_clip_time = norm_t;
                    overlay_state.in_hit_window =
                        norm_t >= def.hit_start_norm && norm_t <= def.hit_end_norm;
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
