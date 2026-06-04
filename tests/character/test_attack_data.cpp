#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/character/core/AttackData.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

static const std::string kAttackFile =
    std::string(ENGINE_SOURCE_DIR) + "/assets/character/combat_attacks.txt";

// Writes an attack-table snippet to a unique file under the OS temp dir and
// returns its path. Caller removes it when done.
static std::string write_temp_attack(const std::string& stem, const std::string& body) {
    static int n = 0;
    ++n;
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() /
        ("combat_attacks_" + stem + "_" + std::to_string(n) + ".txt");
    std::ofstream f(p);
    f << body;
    f.close();
    return p.string();
}

TEST_CASE("parses the tuned combat roster", "[attack_data]") {
    const engine::character::AttackTable table =
        engine::character::AttackData::load(kAttackFile);

    REQUIRE(table.size() == 11);
    REQUIRE(table.count("jab_left"));
    REQUIRE(table.count("uppercut_right"));
    REQUIRE(table.count("knee_strike"));

    const auto& jab = table.at("jab_left");
    REQUIRE(jab.clip == "Jab_Left");
    REQUIRE(jab.clip_start_norm == Catch::Approx(0.15f));
    REQUIRE(jab.clip_end_norm   == Catch::Approx(0.65f));
    REQUIRE(jab.time_scale      == Catch::Approx(1.4f));
    REQUIRE(jab.hit_start_norm  == Catch::Approx(0.35f));
    REQUIRE(jab.hit_end_norm    == Catch::Approx(0.55f));
    REQUIRE(jab.hitstop_frames  == 3);

    const auto& fin = table.at("uppercut_right");
    REQUIRE(fin.hitstop_frames == 6);
}

TEST_CASE("AttackData parses cancel_window fields", "[attack_data]") {
    const std::string tmp = std::string(ENGINE_SOURCE_DIR) +
        "/assets/character/combat_attacks_test_cancel.txt";
    {
        std::ofstream f(tmp);
        f << "attack test_cancel {\n"
          << "    clip TestClip\n"
          << "    hit_window 0.30 0.50\n"
          << "    range 1.0\n"
          << "    radius 0.3\n"
          << "    recovery 0.2\n"
          << "    cancel_window 0.70\n"
          << "    dodge_cancel_window 0.60\n"
          << "}\n";
    }
    const auto table = engine::character::AttackData::load(tmp);
    REQUIRE(table.count("test_cancel"));
    const auto& def = table.at("test_cancel");
    REQUIRE(def.cancel_start_norm == Catch::Approx(0.70f));
    REQUIRE(def.dodge_cancel_start_norm == Catch::Approx(0.60f));
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData rejects cancel before hit end", "[attack_data]") {
    const std::string tmp = std::string(ENGINE_SOURCE_DIR) +
        "/assets/character/combat_attacks_test_bad.txt";
    {
        std::ofstream f(tmp);
        f << "attack bad {\n"
          << "    clip X\n"
          << "    hit_window 0.30 0.50\n"
          << "    range 1.0\n"
          << "    radius 0.3\n"
          << "    recovery 0.2\n"
          << "    cancel_window 0.40\n"
          << "}\n";
    }
    REQUIRE_THROWS(engine::character::AttackData::load(tmp));
    std::filesystem::remove(tmp);
}

TEST_CASE("rejects duplicate field in attack block", "[attack_data]") {
    const std::string bad = R"(
attack test_bad {
    clip Foo
    clip Foo
    hit_window 0.1 0.5
    range 1.0
    radius 0.3
    recovery 0.2
}
)";
    std::ofstream f("_test_attack_dup.txt");
    f << bad;
    f.close();
    REQUIRE_THROWS_AS(engine::character::AttackData::load("_test_attack_dup.txt"),
                      std::runtime_error);
}

TEST_CASE("rejects unknown field in attack block", "[attack_data]") {
    const std::string bad = R"(
attack test_bad {
    clip Foo
    speeed 1.0
    hit_window 0.1 0.5
    range 1.0
    radius 0.3
    recovery 0.2
}
)";
    std::ofstream f("_test_attack_unk.txt");
    f << bad;
    f.close();
    REQUIRE_THROWS_AS(engine::character::AttackData::load("_test_attack_unk.txt"),
                      std::runtime_error);
}

TEST_CASE("rejects missing required field", "[attack_data]") {
    const std::string bad = R"(
attack test_bad {
    clip Foo
    hit_window 0.1 0.5
    range 1.0
    radius 0.3
}
)";
    std::ofstream f("_test_attack_miss.txt");
    f << bad;
    f.close();
    REQUIRE_THROWS_AS(engine::character::AttackData::load("_test_attack_miss.txt"),
                      std::runtime_error);
}

TEST_CASE("AttackData parses clip_window, time_scale, hitstop", "[attack_data]") {
    const std::string tmp = write_temp_attack("trim",
        "attack trimmed {\n"
        "    clip TestClip\n"
        "    clip_window 0.15 0.65\n"
        "    time_scale 1.4\n"
        "    hit_window 0.30 0.50\n"
        "    range 1.0\n    radius 0.3\n    recovery 0.18\n"
        "    cancel_window 0.55\n    dodge_cancel_window 0.45\n"
        "    hitstop 6\n}\n");
    const auto table = engine::character::AttackData::load(tmp);
    REQUIRE(table.count("trimmed"));
    const auto& d = table.at("trimmed");
    REQUIRE(d.clip_start_norm == Catch::Approx(0.15f));
    REQUIRE(d.clip_end_norm   == Catch::Approx(0.65f));
    REQUIRE(d.time_scale      == Catch::Approx(1.4f));
    REQUIRE(d.hitstop_frames  == 6);
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData defaults trim/scale/hitstop when omitted", "[attack_data]") {
    const std::string tmp = write_temp_attack("defaults",
        "attack plain {\n    clip C\n    hit_window 0.30 0.50\n    range 1.0\n"
        "    radius 0.3\n    recovery 0.2\n    cancel_window 0.60\n}\n");
    const auto table = engine::character::AttackData::load(tmp);
    const auto& d = table.at("plain");
    REQUIRE(d.clip_start_norm == Catch::Approx(0.0f));
    REQUIRE(d.clip_end_norm   == Catch::Approx(1.0f));
    REQUIRE(d.time_scale      == Catch::Approx(1.0f));
    REQUIRE(d.hitstop_frames  == 4);
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData rejects clip_window with start >= end", "[attack_data]") {
    const std::string tmp = write_temp_attack("badtrim",
        "attack bad {\n    clip C\n    clip_window 0.70 0.30\n"
        "    hit_window 0.30 0.50\n    range 1.0\n    radius 0.3\n"
        "    recovery 0.2\n    cancel_window 0.60\n}\n");
    REQUIRE_THROWS(engine::character::AttackData::load(tmp));
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData rejects non-positive time_scale", "[attack_data]") {
    const std::string tmp = write_temp_attack("badscale",
        "attack bad {\n    clip C\n    time_scale 0\n"
        "    hit_window 0.30 0.50\n    range 1.0\n    radius 0.3\n"
        "    recovery 0.2\n    cancel_window 0.60\n}\n");
    REQUIRE_THROWS(engine::character::AttackData::load(tmp));
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData rejects fractional hitstop", "[attack_data]") {
    const std::string tmp = write_temp_attack("fracstop",
        "attack bad {\n    clip C\n    hitstop 3.9\n"
        "    hit_window 0.30 0.50\n    range 1.0\n    radius 0.3\n"
        "    recovery 0.2\n    cancel_window 0.60\n}\n");
    REQUIRE_THROWS(engine::character::AttackData::load(tmp));
    std::filesystem::remove(tmp);
}

TEST_CASE("attack_norm_time maps onto trimmed region", "[attack_data]") {
    engine::character::AttackDef d;
    d.clip_start_norm = 0.2f;
    d.clip_end_norm   = 0.6f;            // region = [0.4s, 1.2s] for a 2s clip
    const float dur = 2.0f;
    REQUIRE(engine::character::attack_norm_time(0.4f, d, dur) == Catch::Approx(0.0f));
    REQUIRE(engine::character::attack_norm_time(1.2f, d, dur) == Catch::Approx(1.0f));
    REQUIRE(engine::character::attack_norm_time(0.8f, d, dur) == Catch::Approx(0.5f));
    REQUIRE(engine::character::attack_norm_time(0.0f, d, dur) == Catch::Approx(0.0f)); // clamp
    REQUIRE(engine::character::attack_norm_time(5.0f, d, dur) == Catch::Approx(1.0f)); // clamp
    REQUIRE(engine::character::attack_clip_start_seconds(d, dur) == Catch::Approx(0.4f));
    REQUIRE(engine::character::attack_clip_end_seconds(d, dur)   == Catch::Approx(1.2f));
}
