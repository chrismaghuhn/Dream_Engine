#include "engine/character/core/AttackData.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine::character {

namespace {

// Minimal lexer state for combat_attacks.txt.
struct Lexer {
    std::string source;
    std::string text;
    std::size_t pos = 0;
    int line = 1;
    int col  = 1;

    void advance() {
        if (pos < text.size()) {
            if (text[pos] == '\n') { ++line; col = 1; }
            else { ++col; }
            ++pos;
        }
    }

    void skip_ws_and_comments() {
        while (pos < text.size()) {
            if (text[pos] == '#') {
                while (pos < text.size() && text[pos] != '\n') { advance(); }
            } else if (std::isspace(static_cast<unsigned char>(text[pos]))) {
                advance();
            } else {
                break;
            }
        }
    }

    [[nodiscard]] std::string read_token() {
        skip_ws_and_comments();
        if (pos >= text.size()) { return {}; }
        if (text[pos] == '{' || text[pos] == '}') {
            const char c = text[pos];
            advance();
            return std::string(1, c);
        }
        std::string tok;
        while (pos < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[pos])) &&
               text[pos] != '{' && text[pos] != '}' && text[pos] != '#') {
            tok += text[pos];
            advance();
        }
        return tok;
    }

    [[nodiscard]] float read_float(const std::string& field) {
        const std::string tok = read_token();
        if (tok.empty()) {
            throw std::runtime_error(
                source + ":" + std::to_string(line) + ":" + std::to_string(col) +
                ": expected float for '" + field + "'");
        }
        try { return std::stof(tok); }
        catch (...) {
            throw std::runtime_error(
                source + ":" + std::to_string(line) + ":" + std::to_string(col) +
                ": invalid float '" + tok + "' for '" + field + "'");
        }
    }

    void expect(const std::string& expected) {
        const std::string tok = read_token();
        if (tok != expected) {
            throw std::runtime_error(
                source + ":" + std::to_string(line) + ":" + std::to_string(col) +
                ": expected '" + expected + "' got '" + tok + "'");
        }
    }
};

AttackDef parse_attack_block(Lexer& lex, const std::string& id) {
    AttackDef def;
    def.id = id;

    lex.expect("{");

    std::unordered_set<std::string> seen;

    auto check_dup = [&](const std::string& field) {
        if (!seen.insert(field).second) {
            throw std::runtime_error(
                lex.source + ":" + std::to_string(lex.line) + ":" +
                std::to_string(lex.col) + ": duplicate field '" + field +
                "' in attack '" + id + "'");
        }
    };

    while (true) {
        lex.skip_ws_and_comments();
        if (lex.pos >= lex.text.size()) {
            throw std::runtime_error(
                lex.source + ": unexpected end of file in attack '" + id + "'");
        }
        if (lex.text[lex.pos] == '}') {
            (void)lex.read_token(); // consume '}'
            break;
        }

        const std::string field = lex.read_token();
        if (field.empty()) { break; }

        if (field == "clip") {
            check_dup("clip");
            def.clip = lex.read_token();
        } else if (field == "hit_window") {
            check_dup("hit_window");
            def.hit_start_norm = lex.read_float("hit_window.start");
            def.hit_end_norm   = lex.read_float("hit_window.end");
        } else if (field == "range") {
            check_dup("range");
            def.range = lex.read_float("range");
        } else if (field == "radius") {
            check_dup("radius");
            def.radius = lex.read_float("radius");
        } else if (field == "recovery") {
            check_dup("recovery");
            def.recovery_seconds = lex.read_float("recovery");
        } else if (field == "cancel_window") {
            check_dup("cancel_window");
            def.cancel_start_norm = lex.read_float("cancel_window");
        } else if (field == "dodge_cancel_window") {
            check_dup("dodge_cancel_window");
            def.dodge_cancel_start_norm = lex.read_float("dodge_cancel_window");
        } else {
            throw std::runtime_error(
                lex.source + ":" + std::to_string(lex.line) + ":" +
                std::to_string(lex.col) + ": unknown field '" + field +
                "' in attack '" + id + "'");
        }
    }

    // Validate required fields.
    const std::vector<std::string> required = {
        "clip", "hit_window", "range", "radius", "recovery"};
    for (const auto& req : required) {
        if (seen.find(req) == seen.end()) {
            throw std::runtime_error(
                lex.source + ": missing required field '" + req +
                "' in attack '" + id + "'");
        }
    }

    if (def.hit_start_norm < 0.f || def.hit_end_norm > 1.f ||
        def.hit_start_norm > def.hit_end_norm) {
        throw std::runtime_error(
            lex.source + ": invalid hit_window in attack '" + id + "'");
    }
    if (def.cancel_start_norm < def.hit_end_norm) {
        throw std::runtime_error(
            lex.source + ": cancel_window must be >= hit_window end in attack '" + id + "'");
    }
    if (def.dodge_cancel_start_norm > def.cancel_start_norm) {
        throw std::runtime_error(
            lex.source + ": dodge_cancel_window must be <= cancel_window in attack '" + id + "'");
    }

    return def;
}

} // namespace

AttackTable AttackData::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("AttackData: cannot open '" + path + "'");
    }
    std::ostringstream ss;
    ss << file.rdbuf();

    Lexer lex;
    lex.source = path;
    lex.text   = ss.str();

    AttackTable table;
    while (true) {
        lex.skip_ws_and_comments();
        if (lex.pos >= lex.text.size()) { break; }

        const std::string keyword = lex.read_token();
        if (keyword.empty()) { break; }
        if (keyword != "attack") {
            throw std::runtime_error(
                lex.source + ":" + std::to_string(lex.line) + ":" +
                std::to_string(lex.col) + ": expected 'attack', got '" + keyword + "'");
        }

        const std::string id = lex.read_token();
        if (id.empty() || id == "{" || id == "}") {
            throw std::runtime_error(
                lex.source + ":" + std::to_string(lex.line) + ":" +
                std::to_string(lex.col) + ": expected attack id");
        }
        if (table.count(id)) {
            throw std::runtime_error(
                lex.source + ": duplicate attack id '" + id + "'");
        }

        AttackDef def = parse_attack_block(lex, id);
        table[id] = std::move(def);
    }

    return table;
}

} // namespace engine::character
