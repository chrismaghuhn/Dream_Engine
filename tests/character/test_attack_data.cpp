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

TEST_CASE("parses combat attacks with hit and cancel windows", "[attack_data]") {
    const engine::character::AttackTable table =
        engine::character::AttackData::load(kAttackFile);

    REQUIRE(table.size() == 8);
    REQUIRE(table.count("high_kick"));
    REQUIRE(table.count("elbow_strike"));
    REQUIRE(table.count("counterstrike"));

    const auto& hk = table.at("high_kick");
    REQUIRE(hk.clip == "High_Kick");
    REQUIRE(hk.hit_start_norm == Catch::Approx(0.35f));
    REQUIRE(hk.hit_end_norm   == Catch::Approx(0.48f));
    REQUIRE(hk.range     == Catch::Approx(1.25f));
    REQUIRE(hk.radius    == Catch::Approx(0.35f));
    REQUIRE(hk.recovery_seconds == Catch::Approx(0.25f));
    REQUIRE(hk.cancel_start_norm == Catch::Approx(0.68f));
    REQUIRE(hk.dodge_cancel_start_norm == Catch::Approx(0.58f));

    const auto& es = table.at("elbow_strike");
    REQUIRE(es.hit_start_norm == Catch::Approx(0.32f));
    REQUIRE(es.hit_end_norm   == Catch::Approx(0.44f));
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
