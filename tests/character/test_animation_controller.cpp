#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/character/core/AnimationController.hpp"
#include "engine/character/core/CharacterComponents.hpp"

TEST_CASE("crossfade_to sets blend fields correctly", "[animation]") {
    using namespace engine::character;

    AnimationState anim;
    anim.active_clip = "Walk";
    anim.time_seconds = 0.3f;

    AnimationController::crossfade_to(anim, "High_Kick");

    REQUIRE(anim.blend_clip == "Walk");
    REQUIRE(anim.blend_time == Catch::Approx(0.3f));
    REQUIRE(anim.blend_weight == Catch::Approx(1.f));
    REQUIRE(anim.active_clip == "High_Kick");
    REQUIRE(anim.time_seconds == Catch::Approx(0.f));
    REQUIRE_FALSE(anim.looping);
}

TEST_CASE("tick decays blend_weight to zero", "[animation]") {
    using namespace engine::character;

    AnimationState anim;
    anim.active_clip = "High_Kick";
    anim.blend_clip = "Walk";
    anim.blend_weight = 1.f;
    anim.blend_duration = 0.1f;
    anim.speed = 1.f;

    AnimClip clip;
    clip.name = "High_Kick";
    clip.duration_seconds = 1.f;

    AnimationController::tick(anim, &clip, 0.05f);
    REQUIRE(anim.blend_weight == Catch::Approx(0.5f));

    AnimationController::tick(anim, &clip, 0.05f);
    REQUIRE(anim.blend_weight == Catch::Approx(0.f));
    REQUIRE(anim.blend_clip.empty());
}

TEST_CASE("crossfade_to no-op when clip unchanged", "[animation]") {
    using namespace engine::character;

    AnimationState anim;
    anim.active_clip = "Walk";
    anim.time_seconds = 0.5f;

    AnimationController::crossfade_to(anim, "Walk");

    REQUIRE(anim.blend_clip.empty());
    REQUIRE(anim.time_seconds == Catch::Approx(0.5f));
}

TEST_CASE("select_locomotion returns Idle when standing", "[animation]") {
    using engine::character::AnimationController;
    REQUIRE(AnimationController::select_locomotion(0.0f, true)  == "Idle");
    REQUIRE(AnimationController::select_locomotion(0.05f, true) == "Idle");
    REQUIRE(AnimationController::select_locomotion(1.0f, true)  == "Walk");
    REQUIRE(AnimationController::select_locomotion(4.0f, true)  == "Run");
    // Airborne falls back to Walk (no idle in the air).
    REQUIRE(AnimationController::select_locomotion(0.0f, false) == "Walk");
}
