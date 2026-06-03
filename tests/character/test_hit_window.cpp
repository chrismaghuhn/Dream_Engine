#include <catch2/catch_test_macros.hpp>

#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterComponents.hpp"
#include "engine/character/core/HitDetection.hpp"
#include "engine/movement/core/Components.hpp"

using namespace engine::character;
using namespace engine::movement;

static AttackDef make_def() {
    return AttackDef{"high_kick", "High_Kick", 0.35f, 0.48f, 1.25f, 0.35f, 0.25f, 0.68f, 0.58f};
}

static Transform make_transform(glm::vec3 pos = {0.f, 1.f, 0.f}, float yaw = 0.f) {
    Transform t;
    t.position = pos;
    t.yaw = yaw;
    return t;
}

static Collider box_col(glm::vec3 half = {0.4f, 0.9f, 0.4f}) {
    Collider c;
    c.shape = ColliderShape::Box;
    c.half_extents = half;
    return c;
}

// Build a CombatController that is mid-attack with given elapsed/remaining.
static CombatController attacking(float elapsed, float remaining, float yaw = 0.f) {
    CombatController cc;
    cc.phase          = CombatPhase::Active;
    cc.attack_yaw     = yaw;
    cc.hit_consumed   = false;
    return cc;
}

TEST_CASE("hit fires inside window", "[hit_window]") {
    const AttackDef def = make_def();
    // duration=elapsed+remaining=1.0, normalized=0.40 → inside [0.35,0.48]
    CombatController combat = attacking(0.f, 0.6f);

    AnimationState anim;
    anim.active_clip  = "High_Kick";
    anim.time_seconds = 0.40f;

    // Target placed at exactly `range` ahead along forward (+Z at yaw=0).
    const bool hit = try_hit_in_window(
        combat, anim, def, 1.f,
        make_transform({0.f, 1.f, 0.f}, 0.f),
        make_transform({0.f, 1.f, 1.25f}),
        box_col());

    REQUIRE(hit);
    REQUIRE(combat.hit_consumed);
}

TEST_CASE("hit fires at most once per swing", "[hit_window]") {
    const AttackDef def = make_def();
    CombatController combat = attacking(0.f, 0.6f);

    AnimationState anim;
    anim.active_clip  = "High_Kick";
    anim.time_seconds = 0.40f;

    auto attacker = make_transform({0.f, 1.f, 0.f}, 0.f);
    auto target   = make_transform({0.f, 1.f, 1.25f});
    auto col      = box_col();

    try_hit_in_window(combat, anim, def, 1.f, attacker, target, col);
    REQUIRE(combat.hit_consumed);

    REQUIRE_FALSE(try_hit_in_window(combat, anim, def, 1.f, attacker, target, col));
}

TEST_CASE("no hit before window", "[hit_window]") {
    const AttackDef def = make_def();
    // normalized ≈ 0.11, before [0.35, 0.48]
    CombatController combat = attacking(0.f, 0.8f);

    AnimationState anim;
    anim.active_clip  = "High_Kick";
    anim.time_seconds = 0.10f;

    REQUIRE_FALSE(try_hit_in_window(
        combat, anim, def, 1.f,
        make_transform({0.f, 1.f, 0.f}, 0.f),
        make_transform({0.f, 1.f, 1.25f}),
        box_col()));
    REQUIRE_FALSE(combat.hit_consumed);
}

TEST_CASE("no hit after window", "[hit_window]") {
    const AttackDef def = make_def();
    // normalized ≈ 0.95, after [0.35, 0.48]
    CombatController combat = attacking(0.f, 0.05f);

    AnimationState anim;
    anim.active_clip  = "High_Kick";
    anim.time_seconds = 0.90f;

    REQUIRE_FALSE(try_hit_in_window(
        combat, anim, def, 1.f,
        make_transform({0.f, 1.f, 0.f}, 0.f),
        make_transform({0.f, 1.f, 1.25f}),
        box_col()));
}

TEST_CASE("no hit when not Attacking", "[hit_window]") {
    const AttackDef def = make_def();
    CombatController combat;
    combat.phase = CombatPhase::Idle;

    AnimationState anim;
    anim.time_seconds = 0.40f;

    REQUIRE_FALSE(try_hit_in_window(
        combat, anim, def, 1.f,
        make_transform({0.f, 1.f, 0.f}, 0.f),
        make_transform({0.f, 1.f, 1.25f}),
        box_col()));
}
