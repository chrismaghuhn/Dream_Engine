#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace engine::movement {

// Thrown for any lex/parse/schema error. Message is formatted as
// "<source>:<line>:<col>: <reason>" per the spec's diagnostic requirement.
class ParseException : public std::runtime_error {
public:
    explicit ParseException(std::string message) : std::runtime_error(std::move(message)) {}
};

enum class ArgKind { Number, String, Bool, Ident };

// A single whitespace-separated value following a component name on its line.
struct Arg {
    ArgKind kind = ArgKind::Number;
    double number = 0.0;
    std::string text;   // for String and Ident
    bool boolean = false;
    int line = 0;
    int col = 0;
};

// One component statement, e.g. `transform position 0 1 0 yaw 0`.
// `name` is the leading identifier; `args` is the rest of the line verbatim.
struct Component {
    std::string name;
    int line = 0;
    int col = 0;
    std::vector<Arg> args;
};

// `entity "name" { component-lines }`
struct EntityNode {
    std::string name;
    int line = 0;
    int col = 0;
    std::vector<Component> components;
};

// `arena "id" version N { entity... }`
struct ArenaDocument {
    std::string source;
    std::string id;
    long version = 0;
    int line = 0;
    std::vector<EntityNode> entities;
};

// `override "arena/entity" { component-lines }`
struct OverrideNode {
    std::string persistent_id;
    int line = 0;
    int col = 0;
    std::vector<Component> components;
};

// `save "slot" version N { arena "id" override... }`
struct SaveDocument {
    std::string source;
    std::string slot;
    long version = 0;
    std::string arena_id;
    int line = 0;
    std::vector<OverrideNode> overrides;
};

// Parse a document whose top-level block keyword is `arena` / `save`.
[[nodiscard]] ArenaDocument parse_arena_document(std::string_view text, std::string_view source);
[[nodiscard]] SaveDocument parse_save_document(std::string_view text, std::string_view source);

// Schema helper: turn a component's flat arg list into typed field lookups,
// detecting unknown fields, duplicates, wrong arity, and wrong types.
class FieldReader {
public:
    FieldReader(const Component& component,
                std::string_view source,
                bool leading_shape_ident);

    // Present only when constructed with leading_shape_ident = true.
    [[nodiscard]] const std::string& shape() const { return shape_; }

    [[nodiscard]] bool has(std::string_view field) const;
    [[nodiscard]] double number(std::string_view field) const;
    [[nodiscard]] float number_f(std::string_view field) const;
    void vec3(std::string_view field, float out[3]) const;
    [[nodiscard]] bool boolean(std::string_view field) const;
    [[nodiscard]] const std::string& string(std::string_view field) const;

    // Throw if any present field is not in `allowed`.
    void validate_fields(std::initializer_list<std::string_view> allowed) const;

private:
    struct FieldData {
        std::vector<Arg> values;
        int line = 0;
        int col = 0;
    };
    [[nodiscard]] const FieldData& require(std::string_view field) const;

    const Component& component_;
    std::string source_;
    std::string shape_;
    std::vector<std::pair<std::string, FieldData>> fields_;
};

} // namespace engine::movement
