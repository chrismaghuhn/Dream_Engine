#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterComponents.hpp"
#include "engine/character/core/CombatController.hpp"
#include "engine/character/core/InputBuffer.hpp"
#include "engine/movement/core/Components.hpp"

using namespace engine::character;
using namespace engine::movement;
using Kind = BufferedInput::Kind;

static AttackTable make_table() {
    AttackTable table;
    table["punch"] = AttackDef{"punch", "Punch", 0.30f, 0.50f, 1.0f, 0.4f, 0.2f, 0.70f, 0.60f};
    table["kick"] = AttackDef{"kick", "Kick", 0.40f, 0.60f, 1.2f, 0.4f, 0.2f, 0.72f, 0.62f};
    table["finish"] = AttackDef{"finish", "Finish", 0.30f, 0.50f, 1.0f, 0.4f, 0.3f, 0.75f, 0.65f};
    return table;
}

static std::vector<AnimClip> make_clips() {
    AnimClip punch;
    punch.name = "Punch";
    punch.duration_seconds = 0.5f;
    AnimClip kick;
    kick.name = "Kick";
    kick.duration_seconds = 0.6f;
    AnimClip finish;
    finish.name = "Finish";
    finish.duration_seconds = 0.5f;
    return {punch, kick, finish};
}

static ChainTable make_chains() {
    return {{kLightChain, {"punch", "kick", "finish"}}};
}

static Transform make_transform() {
    Transform tf;
    tf.position = {0.f, 0.f, 0.f};
    tf.yaw = 0.f;
    return tf;
}

TEST_CASE("idle + buffered Light starts first combo hit", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, 1.f / 60.f);

    REQUIRE(combat.phase == CombatPhase::Startup);
    REQUIRE(combat.combo_index == 0);
    REQUIRE(anim.active_clip == "Punch");
    REQUIRE_FALSE(combat.hit_consumed);
}

TEST_CASE("Startup transitions to Active at hit_start_norm", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, 1.f / 60.f);
    REQUIRE(combat.phase == CombatPhase::Startup);

    anim.time_seconds = 0.16f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, 1.f / 60.f);
    REQUIRE(combat.phase == CombatPhase::Active);
}

TEST_CASE("Recovery returns to Idle after recovery_seconds", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;
    const float dt = 1.f / 60.f;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.16f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.26f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Recovery);

    for (int i = 0; i < 13; ++i) {
        combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    }
    REQUIRE(combat.phase == CombatPhase::Idle);
}

TEST_CASE("buffered input in Recovery chains to next attack", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;
    const float dt = 1.f / 60.f;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.16f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.26f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);

    buffer.push(Kind::Light);
    anim.time_seconds = 0.36f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);

    REQUIRE(combat.phase == CombatPhase::Startup);
    REQUIRE(combat.combo_index == 1);
    REQUIRE(anim.active_clip == "Kick");
}

TEST_CASE("attack_yaw locked at combo start", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    tf.yaw = 1.5f;
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, 1.f / 60.f);

    REQUIRE(combat.attack_yaw == Catch::Approx(1.5f));
}

TEST_CASE("hitstop freezes FSM for N frames", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;
    const float dt = 1.f / 60.f;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.16f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Active);

    combat.hitstop_active = true;
    combat.phase_before_hitstop = CombatPhase::Active;
    combat.hitstop_frames = 3;
    anim.speed = 0.f;

    for (int i = 0; i < 3; ++i) {
        combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
        REQUIRE(combat.hitstop_active == (i < 2));
        REQUIRE(combat.phase == CombatPhase::Active);
    }
    REQUIRE_FALSE(combat.hitstop_active);
    REQUIRE(anim.speed == Catch::Approx(1.f));
}

TEST_CASE("start_attack seeds trimmed start time and time_scale", "[combo_fsm]") {
    auto attacks = make_table();
    attacks["punch"].clip_start_norm = 0.2f;   // 0.2 * 0.5s = 0.10s
    attacks["punch"].clip_end_norm   = 0.8f;
    attacks["punch"].time_scale      = 1.5f;
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, 1.f / 60.f);

    REQUIRE(combat.phase == CombatPhase::Startup);
    REQUIRE(anim.time_seconds == Catch::Approx(0.10f));
    REQUIRE(anim.speed == Catch::Approx(1.5f));
}

TEST_CASE("Recovery_DoesNotWaitForRawClipEnd_WhenRawClipHasLongTail", "[combo_fsm]") {
    AttackTable attacks;
    attacks["longtail"] =
        AttackDef{"longtail", "Long", 0.30f, 0.50f, 1.0f, 0.4f, 0.16f, 0.55f, 0.45f};
    std::vector<AnimClip> clips;
    {
        AnimClip c; c.name = "Long"; c.duration_seconds = 5.0f; clips.push_back(c);
    }
    ChainTable chains{{kLightChain, {"longtail"}}};
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;
    const float dt = 1.f / 60.f;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt); // Startup
    anim.time_seconds = 1.6f;   // nt = 0.32 >= hit_start 0.30 -> Active
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Active);
    anim.time_seconds = 2.6f;   // nt = 0.52 > hit_end 0.50 -> Recovery
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Recovery);

    for (int i = 0; i < 11; ++i) {
        combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    }
    REQUIRE(combat.phase == CombatPhase::Idle);   // returned WITHOUT reaching clip end
}

TEST_CASE("start_attack rejects missing clip (no silent fallback)", "[combo_fsm]") {
    AttackTable attacks;
    attacks["ghost"] =
        AttackDef{"ghost", "DoesNotExist", 0.30f, 0.50f, 1.0f, 0.4f, 0.2f, 0.60f, 0.50f};
    std::vector<AnimClip> clips;   // empty -> clip duration resolves to 0
    ChainTable chains{{kLightChain, {"ghost"}}};
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, 1.f / 60.f);

    REQUIRE(combat.phase == CombatPhase::Idle);
    REQUIRE(combat.combo_ids.empty());
}

TEST_CASE("chain-cancel only fires at cancel_start_norm", "[combo_fsm]") {
    auto attacks = make_table();   // punch.cancel_start_norm = 0.70, clip 0.5s
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;
    const float dt = 1.f / 60.f;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.16f;  // Active
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.26f;  // Recovery (nt 0.52 > 0.50)
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Recovery);

    buffer.push(Kind::Light);
    anim.time_seconds = 0.33f;  // nt 0.66 < 0.70: must NOT chain yet
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Recovery);
    REQUIRE(combat.combo_index == 0);

    anim.time_seconds = 0.36f;  // nt 0.72 >= 0.70: chains
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Startup);
    REQUIRE(combat.combo_index == 1);
}
