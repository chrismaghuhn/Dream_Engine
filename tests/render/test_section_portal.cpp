#include <catch2/catch_test_macros.hpp>

#include "engine/render/SectionVisibility.hpp"
#include "engine/world/SectionIndexing.hpp"

namespace {

engine::SectionRenderMeta meta_with_face_solid(engine::Face solid_face, bool solid) {
    engine::SectionRenderMeta meta{};
    meta.is_empty = false;
    if (solid) {
        meta.face_solid_mask |= static_cast<uint8_t>(1u << static_cast<uint32_t>(solid_face));
    }
    return meta;
}

} // namespace

TEST_CASE("section_face_has_portal false when face fully solid") {
    const engine::SectionRenderMeta meta = meta_with_face_solid(engine::Face::PY, true);
    REQUIRE_FALSE(engine::section_face_has_portal(meta, engine::Face::PY));
}

TEST_CASE("section_face_has_portal true when face has air on edge") {
    engine::SectionRenderMeta meta{};
    meta.is_empty = false;
    meta.face_solid_mask = 0;
    REQUIRE(engine::section_face_has_portal(meta, engine::Face::NY));
}

TEST_CASE("sections_connected_portal false when both faces solid") {
    const engine::SectionRenderMeta meta_a = meta_with_face_solid(engine::Face::PX, true);
    engine::Section neighbor{};
    neighbor.render_meta = meta_with_face_solid(engine::Face::NX, true);
    REQUIRE_FALSE(engine::sections_connected_portal(meta_a, engine::Face::PX, &neighbor));
}

TEST_CASE("sections_connected_portal true when one face has portal") {
    const engine::SectionRenderMeta meta_a = meta_with_face_solid(engine::Face::PX, false);
    engine::Section neighbor{};
    neighbor.render_meta = meta_with_face_solid(engine::Face::NX, true);
    REQUIRE(engine::sections_connected_portal(meta_a, engine::Face::PX, &neighbor));
}

TEST_CASE("sections_connected_portal true when neighbor is null") {
    const engine::SectionRenderMeta meta_a = meta_with_face_solid(engine::Face::PX, true);
    REQUIRE(engine::sections_connected_portal(meta_a, engine::Face::PX, nullptr));
}