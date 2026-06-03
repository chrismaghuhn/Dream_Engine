#include "engine/movement/core/TextParser.hpp"

#include <cctype>
#include <cstdlib>

namespace engine::movement {

namespace {

[[noreturn]] void fail(std::string_view source, int line, int col, const std::string& reason) {
    throw ParseException(std::string(source) + ":" + std::to_string(line) + ":" +
                         std::to_string(col) + ": " + reason);
}

enum class TokenType { Ident, String, Number, Bool, LBrace, RBrace, Newline, End };

struct Token {
    TokenType type = TokenType::End;
    std::string text;
    double number = 0.0;
    bool boolean = false;
    int line = 1;
    int col = 1;
};

class Lexer {
public:
    Lexer(std::string_view text, std::string_view source) : text_(text), source_(source) {}

    Token next() {
        skip_inline_trivia();
        const int tok_line = line_;
        const int tok_col = col_;

        if (pos_ >= text_.size()) {
            return Token{TokenType::End, "", 0.0, false, tok_line, tok_col};
        }

        const char c = text_[pos_];
        if (c == '\n') {
            advance();
            return Token{TokenType::Newline, "\\n", 0.0, false, tok_line, tok_col};
        }
        if (c == '{') {
            advance();
            return Token{TokenType::LBrace, "{", 0.0, false, tok_line, tok_col};
        }
        if (c == '}') {
            advance();
            return Token{TokenType::RBrace, "}", 0.0, false, tok_line, tok_col};
        }
        if (c == '"') {
            return lex_string(tok_line, tok_col);
        }
        if (c == '-' || c == '+' || c == '.' ||
            (std::isdigit(static_cast<unsigned char>(c)) != 0)) {
            return lex_number(tok_line, tok_col);
        }
        if (is_ident_start(c)) {
            return lex_ident(tok_line, tok_col);
        }
        fail(source_, tok_line, tok_col, std::string("unexpected character '") + c + "'");
    }

private:
    static bool is_ident_start(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
    }
    static bool is_ident_char(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    void advance() {
        if (text_[pos_] == '\n') {
            ++line_;
            col_ = 1;
        } else {
            ++col_;
        }
        ++pos_;
    }

    // Skip spaces/tabs/CR and comments, but NOT newlines (they are significant).
    void skip_inline_trivia() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\r') {
                advance();
            } else if (c == '#') {
                while (pos_ < text_.size() && text_[pos_] != '\n') {
                    advance();
                }
            } else {
                break;
            }
        }
    }

    Token lex_string(int line, int col) {
        advance(); // opening quote
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"') {
                advance();
                return Token{TokenType::String, std::move(out), 0.0, false, line, col};
            }
            if (c == '\n') {
                fail(source_, line, col, "unterminated string literal");
            }
            if (c == '\\' && pos_ + 1 < text_.size()) {
                const char esc = text_[pos_ + 1];
                if (esc == '"' || esc == '\\') {
                    out.push_back(esc);
                    advance();
                    advance();
                    continue;
                }
            }
            out.push_back(c);
            advance();
        }
        fail(source_, line, col, "unterminated string literal");
    }

    Token lex_number(int line, int col) {
        const std::size_t start = pos_;
        if (text_[pos_] == '-' || text_[pos_] == '+') {
            advance();
        }
        bool any_digit = false;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                any_digit = true;
                advance();
            } else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                advance();
            } else {
                break;
            }
        }
        const std::string slice(text_.substr(start, pos_ - start));
        if (!any_digit) {
            fail(source_, line, col, "malformed number '" + slice + "'");
        }
        char* end = nullptr;
        const double value = std::strtod(slice.c_str(), &end);
        if (end != slice.c_str() + slice.size()) {
            fail(source_, line, col, "malformed number '" + slice + "'");
        }
        return Token{TokenType::Number, slice, value, false, line, col};
    }

    Token lex_ident(int line, int col) {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && is_ident_char(text_[pos_])) {
            advance();
        }
        std::string word(text_.substr(start, pos_ - start));
        if (word == "true" || word == "false") {
            return Token{TokenType::Bool, word, 0.0, word == "true", line, col};
        }
        return Token{TokenType::Ident, std::move(word), 0.0, false, line, col};
    }

    std::string_view text_;
    std::string_view source_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
};

class Parser {
public:
    Parser(std::string_view text, std::string_view source) : lexer_(text, source), source_(source) {
        advance();
    }

    [[nodiscard]] std::string_view source() const { return source_; }

    void advance() { current_ = lexer_.next(); }
    void skip_newlines() {
        while (current_.type == TokenType::Newline) {
            advance();
        }
    }

    [[noreturn]] void error(const std::string& reason) {
        fail(source_, current_.line, current_.col, reason);
    }

    Token expect(TokenType type, const std::string& what) {
        if (current_.type != type) {
            error("expected " + what);
        }
        Token tok = current_;
        advance();
        return tok;
    }

    // header: KEYWORD STRING [ "version" NUMBER ]
    void parse_header(const std::string& keyword,
                      std::string& out_name,
                      long& out_version,
                      int& out_line) {
        skip_newlines();
        if (current_.type != TokenType::Ident || current_.text != keyword) {
            error("expected top-level '" + keyword + "' block");
        }
        out_line = current_.line;
        advance();
        out_name = expect(TokenType::String, "quoted " + keyword + " name").text;
        out_version = 0;
        if (current_.type == TokenType::Ident && current_.text == "version") {
            advance();
            const Token v = expect(TokenType::Number, "version number");
            out_version = static_cast<long>(v.number);
        }
        skip_newlines();
        expect(TokenType::LBrace, "'{'");
        skip_newlines();
    }

    // Parse one component statement (current token = component name ident).
    Component parse_component() {
        Component comp;
        comp.name = current_.text;
        comp.line = current_.line;
        comp.col = current_.col;
        advance();
        while (current_.type != TokenType::Newline && current_.type != TokenType::RBrace &&
               current_.type != TokenType::End) {
            Arg arg;
            arg.line = current_.line;
            arg.col = current_.col;
            switch (current_.type) {
            case TokenType::Number:
                arg.kind = ArgKind::Number;
                arg.number = current_.number;
                break;
            case TokenType::String:
                arg.kind = ArgKind::String;
                arg.text = current_.text;
                break;
            case TokenType::Bool:
                arg.kind = ArgKind::Bool;
                arg.boolean = current_.boolean;
                break;
            case TokenType::Ident:
                arg.kind = ArgKind::Ident;
                arg.text = current_.text;
                break;
            default:
                error("unexpected token in component '" + comp.name + "'");
            }
            comp.args.push_back(std::move(arg));
            advance();
        }
        return comp;
    }

    std::vector<Component> parse_component_block() {
        std::vector<Component> components;
        skip_newlines();
        while (current_.type != TokenType::RBrace) {
            if (current_.type == TokenType::End) {
                error("unexpected end of file inside block");
            }
            if (current_.type != TokenType::Ident) {
                error("expected component name");
            }
            components.push_back(parse_component());
            skip_newlines();
        }
        expect(TokenType::RBrace, "'}'");
        return components;
    }

    Token& current() { return current_; }

private:
    Lexer lexer_;
    std::string_view source_;
    Token current_;
};

} // namespace

ArenaDocument parse_arena_document(std::string_view text, std::string_view source) {
    Parser parser(text, source);
    ArenaDocument doc;
    doc.source = std::string(source);
    parser.parse_header("arena", doc.id, doc.version, doc.line);

    parser.skip_newlines();
    while (parser.current().type != TokenType::RBrace) {
        if (parser.current().type == TokenType::End) {
            parser.error("unexpected end of file: missing closing '}' for arena");
        }
        if (parser.current().type != TokenType::Ident || parser.current().text != "entity") {
            parser.error("expected 'entity' block inside arena");
        }
        EntityNode entity;
        entity.line = parser.current().line;
        entity.col = parser.current().col;
        parser.advance();
        entity.name = parser.expect(TokenType::String, "quoted entity name").text;
        parser.skip_newlines();
        parser.expect(TokenType::LBrace, "'{'");
        entity.components = parser.parse_component_block();
        doc.entities.push_back(std::move(entity));
        parser.skip_newlines();
    }
    return doc;
}

SaveDocument parse_save_document(std::string_view text, std::string_view source) {
    Parser parser(text, source);
    SaveDocument doc;
    doc.source = std::string(source);
    parser.parse_header("save", doc.slot, doc.version, doc.line);

    bool arena_seen = false;
    parser.skip_newlines();
    while (parser.current().type != TokenType::RBrace) {
        if (parser.current().type == TokenType::End) {
            parser.error("unexpected end of file: missing closing '}' for save");
        }
        if (parser.current().type != TokenType::Ident) {
            parser.error("expected 'arena' or 'override' directive");
        }
        if (parser.current().text == "arena") {
            parser.advance();
            doc.arena_id = parser.expect(TokenType::String, "quoted arena id").text;
            arena_seen = true;
            parser.skip_newlines();
        } else if (parser.current().text == "override") {
            OverrideNode node;
            node.line = parser.current().line;
            node.col = parser.current().col;
            parser.advance();
            node.persistent_id = parser.expect(TokenType::String, "quoted PersistentId").text;
            parser.skip_newlines();
            parser.expect(TokenType::LBrace, "'{'");
            node.components = parser.parse_component_block();
            doc.overrides.push_back(std::move(node));
            parser.skip_newlines();
        } else {
            parser.error("unexpected directive '" + parser.current().text + "' in save");
        }
    }
    if (!arena_seen) {
        fail(source, doc.line, 1, "save block missing required 'arena' reference");
    }
    return doc;
}

// ---------------------------------------------------------------------------
// FieldReader
// ---------------------------------------------------------------------------

FieldReader::FieldReader(const Component& component,
                         std::string_view source,
                         bool leading_shape_ident)
    : component_(component), source_(source) {
    std::size_t i = 0;
    if (leading_shape_ident) {
        if (component.args.empty() || component.args[0].kind != ArgKind::Ident) {
            fail(source_, component.line, component.col,
                 "component '" + component.name + "' expects a shape identifier");
        }
        shape_ = component.args[0].text;
        i = 1;
    }

    while (i < component.args.size()) {
        const Arg& key = component.args[i];
        if (key.kind != ArgKind::Ident) {
            fail(source_, key.line, key.col,
                 "expected field name in component '" + component.name + "'");
        }
        for (const auto& existing : fields_) {
            if (existing.first == key.text) {
                fail(source_, key.line, key.col,
                     "duplicate field '" + key.text + "' in component '" + component.name + "'");
            }
        }
        FieldData data;
        data.line = key.line;
        data.col = key.col;
        ++i;
        while (i < component.args.size() && component.args[i].kind != ArgKind::Ident) {
            data.values.push_back(component.args[i]);
            ++i;
        }
        fields_.emplace_back(key.text, std::move(data));
    }
}

const FieldReader::FieldData& FieldReader::require(std::string_view field) const {
    for (const auto& entry : fields_) {
        if (entry.first == field) {
            return entry.second;
        }
    }
    fail(source_, component_.line, component_.col,
         "component '" + component_.name + "' missing required field '" + std::string(field) + "'");
}

bool FieldReader::has(std::string_view field) const {
    for (const auto& entry : fields_) {
        if (entry.first == field) {
            return true;
        }
    }
    return false;
}

double FieldReader::number(std::string_view field) const {
    const FieldData& data = require(field);
    if (data.values.size() != 1 || data.values[0].kind != ArgKind::Number) {
        fail(source_, data.line, data.col,
             "field '" + std::string(field) + "' expects a single number");
    }
    return data.values[0].number;
}

float FieldReader::number_f(std::string_view field) const {
    return static_cast<float>(number(field));
}

void FieldReader::vec3(std::string_view field, float out[3]) const {
    const FieldData& data = require(field);
    if (data.values.size() != 3) {
        fail(source_, data.line, data.col,
             "field '" + std::string(field) + "' expects 3 numbers");
    }
    for (int k = 0; k < 3; ++k) {
        if (data.values[k].kind != ArgKind::Number) {
            fail(source_, data.values[k].line, data.values[k].col,
                 "field '" + std::string(field) + "' expects numbers");
        }
        out[k] = static_cast<float>(data.values[k].number);
    }
}

bool FieldReader::boolean(std::string_view field) const {
    const FieldData& data = require(field);
    if (data.values.size() != 1 || data.values[0].kind != ArgKind::Bool) {
        fail(source_, data.line, data.col,
             "field '" + std::string(field) + "' expects a boolean");
    }
    return data.values[0].boolean;
}

const std::string& FieldReader::string(std::string_view field) const {
    const FieldData& data = require(field);
    if (data.values.size() != 1 || data.values[0].kind != ArgKind::String) {
        fail(source_, data.line, data.col,
             "field '" + std::string(field) + "' expects a string");
    }
    return data.values[0].text;
}

void FieldReader::validate_fields(std::initializer_list<std::string_view> allowed) const {
    for (const auto& entry : fields_) {
        bool ok = false;
        for (std::string_view candidate : allowed) {
            if (entry.first == candidate) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            fail(source_, entry.second.line, entry.second.col,
                 "unknown field '" + entry.first + "' in component '" + component_.name + "'");
        }
    }
}

} // namespace engine::movement
