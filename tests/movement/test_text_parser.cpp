#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "engine/movement/core/TextParser.hpp"

using Catch::Matchers::ContainsSubstring;
using namespace engine::movement;

namespace {
constexpr const char* kValidArena = R"(# comment line
arena "movement_test" version 2
{
    entity "player"
    {
        transform position 0 1.1 0 yaw 0   # inline comment
        collider capsule radius 0.35 height 1.75
    }

    entity "floor"
    {
        collider box half_extents 50 0.5 50
    }
}
)";
}

TEST_CASE("parser reads arena, version, entities and components") {
    const ArenaDocument doc = parse_arena_document(kValidArena, "test.arena");
    REQUIRE(doc.id == "movement_test");
    REQUIRE(doc.version == 2);
    REQUIRE(doc.entities.size() == 2);
    REQUIRE(doc.entities[0].name == "player");
    REQUIRE(doc.entities[0].components.size() == 2);
    REQUIRE(doc.entities[0].components[0].name == "transform");
    REQUIRE(doc.entities[1].name == "floor");
}

TEST_CASE("field reader reads vec3, scalar and shape") {
    const ArenaDocument doc = parse_arena_document(kValidArena, "test.arena");
    const Component& transform = doc.entities[0].components[0];
    FieldReader tf(transform, "test.arena", false);
    float pos[3];
    tf.vec3("position", pos);
    REQUIRE(pos[1] == 1.1f);
    REQUIRE(tf.number("yaw") == 0.0);

    const Component& collider = doc.entities[0].components[1];
    FieldReader col(collider, "test.arena", true);
    REQUIRE(col.shape() == "capsule");
    REQUIRE(col.number_f("radius") == 0.35f);
}

TEST_CASE("comments are ignored") {
    const ArenaDocument doc =
        parse_arena_document("# only a comment\narena \"x\" version 1\n{\n}\n", "c.arena");
    REQUIRE(doc.id == "x");
    REQUIRE(doc.entities.empty());
}

TEST_CASE("boolean tokens parse in save overrides") {
    const char* src = R"(save "slot" version 1
{
    arena "movement_test"
    override "movement_test/player"
    {
        player_controller velocity 0 0 0 grounded true
    }
}
)";
    const SaveDocument doc = parse_save_document(src, "s.save");
    REQUIRE(doc.arena_id == "movement_test");
    REQUIRE(doc.overrides.size() == 1);
    const Component& pc = doc.overrides[0].components[0];
    FieldReader fields(pc, "s.save", false);
    REQUIRE(fields.boolean("grounded") == true);
}

TEST_CASE("unknown field is rejected with line and column") {
    const char* src = R"(arena "x" version 1
{
    entity "p"
    {
        transform position 0 0 0 speeed 5
    }
}
)";
    const ArenaDocument doc = parse_arena_document(src, "bad.arena");
    FieldReader fields(doc.entities[0].components[0], "bad.arena", false);
    REQUIRE_THROWS_MATCHES(
        fields.validate_fields({"position", "yaw"}),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("bad.arena:5:") &&
                                        ContainsSubstring("speeed")));
}

TEST_CASE("duplicate field is rejected") {
    const char* src = R"(arena "x" version 1
{
    entity "p"
    {
        transform position 0 0 0 position 1 1 1
    }
}
)";
    const ArenaDocument doc = parse_arena_document(src, "dup.arena");
    REQUIRE_THROWS_MATCHES(
        FieldReader(doc.entities[0].components[0], "dup.arena", false),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("duplicate field") &&
                                        ContainsSubstring("position")));
}

TEST_CASE("missing required field reports the component") {
    const char* src = R"(arena "x" version 1
{
    entity "p"
    {
        transform yaw 0
    }
}
)";
    const ArenaDocument doc = parse_arena_document(src, "m.arena");
    FieldReader fields(doc.entities[0].components[0], "m.arena", false);
    REQUIRE_THROWS_MATCHES(
        fields.number("position"),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("missing required field") &&
                                        ContainsSubstring("position")));
}

TEST_CASE("error message carries line and column") {
    // The '%' is an unexpected character at a known position.
    const char* src = "arena \"x\" version 1\n{\n  entity \"p\" { transform position 0 % 0 }\n}\n";
    REQUIRE_THROWS_MATCHES(
        parse_arena_document(src, "col.arena"),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("col.arena:3:")));
}

TEST_CASE("malformed number throws") {
    const char* src = "arena \"x\" version 1\n{\n  entity \"p\" { transform yaw 1.2.3.4abc }\n}\n";
    REQUIRE_THROWS_AS(parse_arena_document(src, "n.arena"), ParseException);
}

TEST_CASE("unterminated block throws") {
    REQUIRE_THROWS_AS(parse_arena_document("arena \"x\" version 1\n{\n", "u.arena"),
                      ParseException);
}

TEST_CASE("non-arena top-level keyword is rejected") {
    REQUIRE_THROWS_MATCHES(
        parse_arena_document("world \"x\" version 1\n{\n}\n", "w.arena"),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("arena")));
}
