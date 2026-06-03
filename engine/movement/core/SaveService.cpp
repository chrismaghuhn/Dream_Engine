#include "engine/movement/core/SaveService.hpp"

#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/MovementWorld.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

namespace engine::movement {

namespace {

[[noreturn]] void fail(std::string_view source, int line, int col, const std::string& reason) {
    throw ParseException(std::string(source) + ":" + std::to_string(line) + ":" +
                         std::to_string(col) + ": " + reason);
}

} // namespace

std::filesystem::path SaveService::default_save_path() {
    return std::filesystem::path(ENGINE_SOURCE_DIR) / "saves" / "movement_test.save";
}

std::string SaveService::serialize(const MovementWorld& world,
                                   std::string_view slot,
                                   std::string_view arena_id) {
    std::ostringstream out;
    out << std::setprecision(9);
    out << "# movement save file\n";
    out << "save \"" << slot << "\" version 1\n{\n";
    out << "    arena \"" << arena_id << "\"\n\n";

    world.controllers().for_each([&](std::uint32_t index, const PlayerController& pc) {
        const EntityId id = EntityId{index, 0u}; // stores key off index only
        const PersistentId* pid = world.persistent_id_by_index(index);
        if (pid == nullptr) {
            return;
        }
        const Transform* tf = world.transforms().get(id);

        out << "    override \"" << pid->str() << "\"\n    {\n";
        if (tf != nullptr) {
            out << "        transform position " << tf->position.x << " " << tf->position.y << " "
                << tf->position.z << " yaw " << tf->yaw << "\n";
        }
        out << "        player_controller velocity " << pc.velocity.x << " " << pc.velocity.y << " "
            << pc.velocity.z << " grounded " << (pc.grounded ? "true" : "false") << "\n";

        if (const CameraRig* rig = world.camera_rigs().get(id)) {
            out << "        camera_rig yaw " << rig->yaw << " pitch " << glm::degrees(rig->pitch)
                << " distance " << rig->distance << " height " << rig->height << "\n";
        }
        out << "    }\n";
    });

    out << "}\n";
    return out.str();
}

void SaveService::save(const std::filesystem::path& path,
                       const MovementWorld& world,
                       std::string_view slot,
                       std::string_view arena_id) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw ParseException(path.string() + ":0:0: cannot open save file for writing");
    }
    const std::string text = serialize(world, slot, arena_id);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void SaveService::apply_document(const SaveDocument& doc,
                                 MovementWorld& world,
                                 std::string_view expected_arena_id) {
    if (!expected_arena_id.empty() && doc.arena_id != expected_arena_id) {
        fail(doc.source, doc.line, 1,
             "save references arena '" + doc.arena_id + "' but world is arena '" +
                 std::string(expected_arena_id) + "'");
    }

    for (const OverrideNode& node : doc.overrides) {
        const PersistentId pid = PersistentId::from_string(node.persistent_id);
        const EntityId id = world.find(pid);
        if (id.is_null()) {
            fail(doc.source, node.line, node.col,
                 "override targets unknown PersistentId '" + node.persistent_id + "'");
        }

        for (const Component& comp : node.components) {
            if (comp.name == "transform") {
                FieldReader fields(comp, doc.source, false);
                fields.validate_fields({"position", "yaw"});
                Transform* tf = world.transforms().get(id);
                Transform local;
                Transform& target = tf != nullptr ? *tf : local;
                float pos[3] = {0, 0, 0};
                fields.vec3("position", pos);
                target.position = glm::vec3(pos[0], pos[1], pos[2]);
                if (fields.has("yaw")) {
                    target.yaw = fields.number_f("yaw");
                }
                target.sync_previous(); // previous = current on load
                if (tf == nullptr) {
                    world.transforms().add(id, target);
                }
            } else if (comp.name == "player_controller") {
                FieldReader fields(comp, doc.source, false);
                fields.validate_fields({"velocity", "grounded", "speed", "jump_velocity", "gravity"});
                PlayerController* pc = world.controllers().get(id);
                PlayerController local;
                PlayerController& target = pc != nullptr ? *pc : local;
                if (fields.has("velocity")) {
                    float v[3] = {0, 0, 0};
                    fields.vec3("velocity", v);
                    target.velocity = glm::vec3(v[0], v[1], v[2]);
                }
                if (fields.has("grounded")) {
                    target.grounded = fields.boolean("grounded");
                }
                if (fields.has("speed")) {
                    target.speed = fields.number_f("speed");
                }
                if (fields.has("jump_velocity")) {
                    target.jump_velocity = fields.number_f("jump_velocity");
                }
                if (fields.has("gravity")) {
                    target.gravity = fields.number_f("gravity");
                }
                if (pc == nullptr) {
                    world.controllers().add(id, target);
                }
            } else if (comp.name == "camera_rig") {
                FieldReader fields(comp, doc.source, false);
                fields.validate_fields({"yaw", "pitch", "distance", "height"});
                CameraRig* rig = world.camera_rigs().get(id);
                CameraRig local;
                CameraRig& target = rig != nullptr ? *rig : local;
                target.yaw = fields.number_f("yaw");
                target.distance = fields.number_f("distance");
                target.height = fields.number_f("height");
                target.pitch = glm::radians(fields.number_f("pitch"));
                if (rig == nullptr) {
                    target.target = id;
                    world.camera_rigs().add(id, target);
                }
            } else {
                fail(doc.source, comp.line, comp.col,
                     "unknown component '" + comp.name + "' in save override");
            }
        }
    }
}

void SaveService::load_from_string(std::string_view text,
                                   std::string_view source,
                                   MovementWorld& world,
                                   std::string_view expected_arena_id) {
    const SaveDocument doc = parse_save_document(text, source);
    apply_document(doc, world, expected_arena_id);
}

void SaveService::load(const std::filesystem::path& path,
                       MovementWorld& world,
                       std::string_view expected_arena_id) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ParseException(path.string() + ":0:0: cannot open save file");
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    const std::string text = stream.str();
    load_from_string(text, path.string(), world, expected_arena_id);
}

} // namespace engine::movement
