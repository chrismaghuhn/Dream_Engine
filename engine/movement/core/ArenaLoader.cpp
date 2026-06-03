#include "engine/movement/core/ArenaLoader.hpp"

#include "engine/movement/core/CameraSystem.hpp"
#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/MovementWorld.hpp"

#include <fstream>
#include <sstream>

namespace engine::movement {

namespace {

[[noreturn]] void fail(std::string_view source, int line, int col, const std::string& reason) {
    throw ParseException(std::string(source) + ":" + std::to_string(line) + ":" +
                         std::to_string(col) + ": " + reason);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ParseException(path.string() + ":0:0: cannot open file");
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

void apply_transform(const Component& comp, std::string_view source, Transform& out) {
    FieldReader fields(comp, source, /*leading_shape_ident=*/false);
    fields.validate_fields({"position", "yaw"});
    float pos[3] = {0, 0, 0};
    fields.vec3("position", pos);
    out.position = glm::vec3(pos[0], pos[1], pos[2]);
    out.yaw = fields.has("yaw") ? fields.number_f("yaw") : 0.f;
    out.sync_previous();
}

void apply_player_controller(const Component& comp, std::string_view source, PlayerController& out) {
    FieldReader fields(comp, source, /*leading_shape_ident=*/false);
    fields.validate_fields({"speed", "jump_velocity", "gravity"});
    out.speed = fields.number_f("speed");
    out.jump_velocity = fields.number_f("jump_velocity");
    out.gravity = fields.number_f("gravity");
}

void apply_collider(const Component& comp, std::string_view source, Collider& out) {
    FieldReader fields(comp, source, /*leading_shape_ident=*/true);
    const std::string& shape = fields.shape();
    if (shape == "capsule") {
        out.shape = ColliderShape::Capsule;
        out.is_static = false;
        fields.validate_fields({"radius", "height"});
        out.radius = fields.number_f("radius");
        out.height = fields.number_f("height");
    } else if (shape == "box") {
        out.shape = ColliderShape::Box;
        out.is_static = true;
        fields.validate_fields({"half_extents"});
        float he[3] = {0.5f, 0.5f, 0.5f};
        fields.vec3("half_extents", he);
        out.half_extents = glm::vec3(he[0], he[1], he[2]);
    } else if (shape == "ground" || shape == "ground_plane") {
        out.shape = ColliderShape::GroundPlane;
        out.is_static = true;
        fields.validate_fields({});
    } else {
        fail(source, comp.line, comp.col, "unknown collider shape '" + shape + "'");
    }
}

void apply_camera_rig(const Component& comp, std::string_view source, CameraRig& out) {
    FieldReader fields(comp, source, /*leading_shape_ident=*/false);
    fields.validate_fields({"yaw", "pitch", "distance", "height"});
    out.yaw = fields.number_f("yaw");
    camera::configure(out, fields.number_f("distance"), fields.number_f("height"));
    camera::set_pitch_degrees(out, fields.number_f("pitch")); // file stores degrees
}

} // namespace

ArenaLoadResult load_arena_document(const ArenaDocument& doc, MovementWorld& world) {
    ArenaLoadResult result;
    result.arena_id = doc.id;
    result.version = doc.version;

    for (const EntityNode& entity : doc.entities) {
        const PersistentId pid = PersistentId::make(doc.id, entity.name);
        const EntityId id = world.spawn(pid);
        bool has_transform = false;

        for (const Component& comp : entity.components) {
            if (comp.name == "transform") {
                Transform tf;
                apply_transform(comp, doc.source, tf);
                world.transforms().add(id, tf);
                has_transform = true;
            } else if (comp.name == "player_controller") {
                PlayerController pc;
                apply_player_controller(comp, doc.source, pc);
                world.controllers().add(id, pc);
                if (result.player.is_null()) {
                    result.player = id;
                }
            } else if (comp.name == "collider") {
                Collider col;
                apply_collider(comp, doc.source, col);
                world.colliders().add(id, col);
            } else if (comp.name == "camera_rig") {
                CameraRig rig;
                apply_camera_rig(comp, doc.source, rig);
                rig.target = id;
                world.camera_rigs().add(id, rig);
            } else if (comp.name == "debug_name") {
                if (comp.args.size() != 1 || comp.args[0].kind != ArgKind::String) {
                    fail(doc.source, comp.line, comp.col,
                         "component 'debug_name' expects a single string");
                }
                world.debug_names().add(id, DebugName{comp.args[0].text});
            } else {
                fail(doc.source, comp.line, comp.col,
                     "unknown component '" + comp.name + "' in entity '" + entity.name + "'");
            }
        }

        if (!has_transform) {
            // Every entity needs a transform so collision/render can place it.
            world.transforms().add(id, Transform{});
        }
        // Give every named entity a debug name fallback for the overlay.
        if (!world.debug_names().has(id)) {
            world.debug_names().add(id, DebugName{pid.str()});
        }
    }

    return result;
}

ArenaLoadResult load_arena_from_string(std::string_view text,
                                       std::string_view source,
                                       MovementWorld& world) {
    const ArenaDocument doc = parse_arena_document(text, source);
    return load_arena_document(doc, world);
}

ArenaLoadResult load_arena_file(const std::filesystem::path& path, MovementWorld& world) {
    const std::string text = read_file(path);
    return load_arena_from_string(text, path.string(), world);
}

} // namespace engine::movement
