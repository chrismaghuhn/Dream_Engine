#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterComponents.hpp"
#include "engine/character/core/CombatController.hpp"
#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/InputSnapshot.hpp"

using namespace engine::character;
using namespace engine::movement;

// Helpers to build minimal test fixtures.
static AttackTable make_table() {
    AttackTable t;
    t["punch"]  = AttackDef{"punch",  "Punch",  0.3f, 0.5f, 1.0f, 0.4f, 0.2f};
    t["kick"]   = AttackDef{"kick",   "Kick",   0.4f, 0.6f, 1.2f, 0.4f, 0.2f};
    t["finish"] = AttackDef{"finish", "Finish", 0.3f, 0.5f, 1.0f, 0.4f, 0.3f};
    return t;
}

static std::vector<AnimClip> make_clips() {
    AnimClip a, b, c;
    a.name = "Punch";  a.duration_seconds = 0.5f;
    b.name = "Kick";   b.duration_seconds = 0.6f;
    c.name = "Finish"; c.duration_seconds = 0.5f;
    return {a, b, c};
}

static CombatController make_combat() {
    CombatController cc;
    cc.combo_ids = {"punch", "kick", "finish"};
    return cc;
}

static Transform make_transform() {
    Transform tf;
    tf.position = {0.f, 0.f, 0.f};
    tf.yaw = 0.f;
    return tf;
}

TEST_CASE("idle + attack_pressed starts first combo hit", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips   = make_clips();
    auto combat  = make_combat();
    auto tf      = make_transform();
    AnimationState anim;
    InputSnapshot input;
    input.attack_pressed = true;

    combat_tick(combat, tf, anim, input, attacks, clips, 1.f / 60.f);

    REQUIRE(combat.phase == CombatPhase::Attacking);
    REQUIRE(combat.combo_index == 0);
    REQUIRE(anim.active_clip == "Punch");
    REQUIRE_FALSE(input.attack_pressed); // consumed
    REQUIRE(combat.hit_consumed == false);
}

TEST_CASE("chains three attacks then enters recovery", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips   = make_clips();
    auto combat  = make_combat();
    auto tf      = make_transform();
    AnimationState anim;
    InputSnapshot input;
    input.attack_pressed = true;

    const float dt = 1.f / 60.f;

    // Start combo.
    combat_tick(combat, tf, anim, input, attacks, clips, dt);
    REQUIRE(combat.phase == CombatPhase::Attacking);
    REQUIRE(combat.combo_index == 0);

    // Advance past Punch (0.5s).
    for (int i = 0; i < 32; ++i) {
        combat_tick(combat, tf, anim, input, attacks, clips, dt);
    }
    // Should have advanced to Kick.
    REQUIRE(combat.combo_index >= 1);

    // Advance past Kick (0.6s).
    for (int i = 0; i < 40; ++i) {
        combat_tick(combat, tf, anim, input, attacks, clips, dt);
    }
    // Should have advanced to Finish.
    REQUIRE(combat.combo_index >= 2);

    // Advance past Finish (0.5s).
    for (int i = 0; i < 35; ++i) {
        combat_tick(combat, tf, anim, input, attacks, clips, dt);
    }
    // Should be in Recovery.
    REQUIRE(combat.phase == CombatPhase::Recovery);

    // Advance through recovery (0.3s).
    for (int i = 0; i < 25; ++i) {
        combat_tick(combat, tf, anim, input, attacks, clips, dt);
    }
    REQUIRE(combat.phase == CombatPhase::Idle);
}

TEST_CASE("ignores attack_pressed while Attacking", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips   = make_clips();
    auto combat  = make_combat();
    auto tf      = make_transform();
    AnimationState anim;
    InputSnapshot input;
    input.attack_pressed = true;

    const float dt = 1.f / 60.f;
    combat_tick(combat, tf, anim, input, attacks, clips, dt);
    REQUIRE(combat.phase == CombatPhase::Attacking);

    // Try to attack again mid-combo.
    input.attack_pressed = true;
    combat_tick(combat, tf, anim, input, attacks, clips, dt);
    REQUIRE(input.attack_pressed == false);   // consumed/cleared
    REQUIRE(combat.phase == CombatPhase::Attacking); // still in combo
    REQUIRE(combat.combo_index == 0);         // did NOT re-start combo
}

TEST_CASE("attack_yaw locked at attack start", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips   = make_clips();
    auto combat  = make_combat();
    auto tf      = make_transform();
    tf.yaw = 1.5f;
    AnimationState anim;
    InputSnapshot input;
    input.attack_pressed = true;

    combat_tick(combat, tf, anim, input, attacks, clips, 1.f / 60.f);

    REQUIRE(combat.attack_yaw == 1.5f);
}
