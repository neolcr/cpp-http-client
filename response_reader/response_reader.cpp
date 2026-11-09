// Standalone C++20 application; no project headers or external libraries required.
// zig c++ -std=c++20 -O2 response_reader.cpp -o response_reader.exe
#include <charconv>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace response_reader {

struct JsonValue {
    char kind = 0;
    std::string text;
    std::string name;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::vector<JsonValue> children;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    JsonValue parse() {
        auto value = parse_value(0);
        skip_space();
        if (position_ != input_.size()) fail();
        return value;
    }

private:
    [[noreturn]] void fail() const {
        throw std::runtime_error("Invalid JSON at byte " + std::to_string(position_));
    }

    void skip_space() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\t' ||
                input_[position_] == '\r' || input_[position_] == '\n')) ++position_;
    }

    bool consume(char expected) {
        if (position_ == input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    void expect(char expected) {
        if (!consume(expected)) fail();
    }

    unsigned int hex_quad() {
        unsigned int value = 0;
        for (int index = 0; index < 4; ++index) {
            if (position_ == input_.size()) fail();
            const char digit = input_[position_++];
            value <<= 4;
            if (digit >= '0' && digit <= '9') value += digit - '0';
            else if (digit >= 'a' && digit <= 'f') value += digit - 'a' + 10;
            else if (digit >= 'A' && digit <= 'F') value += digit - 'A' + 10;
            else fail();
        }
        return value;
    }

    void append_unicode(std::string& text) {
        unsigned int point = hex_quad();
        if (point >= 0xd800 && point <= 0xdbff) {
            expect('\\');
            expect('u');
            const unsigned int low = hex_quad();
            if (low < 0xdc00 || low > 0xdfff) fail();
            point = 0x10000 + ((point - 0xd800) << 10) + low - 0xdc00;
        } else if (point >= 0xdc00 && point <= 0xdfff) {
            fail();
        }
        if (point <= 0x7f) {
            text.push_back(static_cast<char>(point));
        } else if (point <= 0x7ff) {
            text.push_back(static_cast<char>(0xc0 | (point >> 6)));
            text.push_back(static_cast<char>(0x80 | (point & 0x3f)));
        } else if (point <= 0xffff) {
            text.push_back(static_cast<char>(0xe0 | (point >> 12)));
            text.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
            text.push_back(static_cast<char>(0x80 | (point & 0x3f)));
        } else {
            text.push_back(static_cast<char>(0xf0 | (point >> 18)));
            text.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3f)));
            text.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
            text.push_back(static_cast<char>(0x80 | (point & 0x3f)));
        }
    }

    std::string parse_string() {
        expect('"');
        std::string text;
        while (position_ < input_.size()) {
            const auto ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"') return text;
            if (ch < 0x20) fail();
            if (ch != '\\') {
                text.push_back(static_cast<char>(ch));
                continue;
            }
            if (position_ == input_.size()) fail();
            switch (input_[position_++]) {
                case '"': text.push_back('"'); break;
                case '\\': text.push_back('\\'); break;
                case '/': text.push_back('/'); break;
                case 'b': text.push_back('\b'); break;
                case 'f': text.push_back('\f'); break;
                case 'n': text.push_back('\n'); break;
                case 'r': text.push_back('\r'); break;
                case 't': text.push_back('\t'); break;
                case 'u': append_unicode(text); break;
                default: fail();
            }
        }
        fail();
    }

    bool digit() const {
        return position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9';
    }

    void digits() {
        if (!digit()) fail();
        while (digit()) ++position_;
    }

    void parse_number() {
        consume('-');
        if (!consume('0')) digits();
        if (consume('.')) digits();
        if (consume('e') || consume('E')) {
            if (!consume('+')) consume('-');
            digits();
        }
    }

    JsonValue parse_value(std::size_t depth) {
        if (depth > 128) throw std::runtime_error("JSON nesting exceeds 128 levels");
        skip_space();
        if (position_ == input_.size()) fail();
        JsonValue value;
        value.begin = position_;
        value.kind = input_[position_];
        if (value.kind == '{' || value.kind == '[') {
            ++position_;
            const char closing = value.kind == '{' ? '}' : ']';
            skip_space();
            if (!consume(closing)) {
                do {
                    skip_space();
                    std::string name;
                    if (value.kind == '{') {
                        name = parse_string();
                        for (const auto& child : value.children) {
                            if (child.name == name) throw std::runtime_error("Duplicate JSON field: " + name);
                        }
                        skip_space();
                        expect(':');
                    }
                    auto child = parse_value(depth + 1);
                    child.name = std::move(name);
                    value.children.push_back(std::move(child));
                    skip_space();
                } while (consume(','));
                expect(closing);
            }
        } else if (value.kind == '"') {
            value.text = parse_string();
        } else if (value.kind == 't' || value.kind == 'f' || value.kind == 'n') {
            const std::string_view literal = value.kind == 't' ? "true" : value.kind == 'f' ? "false" : "null";
            if (input_.substr(position_, literal.size()) != literal) fail();
            position_ += literal.size();
        } else if (value.kind == '-' || digit()) {
            value.kind = '0';
            parse_number();
        } else {
            fail();
        }
        value.end = position_;
        return value;
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

const JsonValue* member(const JsonValue& object, std::string_view name) {
    if (object.kind != '{') return nullptr;
    for (const auto& child : object.children) {
        if (child.name == name) return &child;
    }
    return nullptr;
}

void find_field(const JsonValue& value, std::string_view name, const JsonValue*& match) {
    for (const auto& child : value.children) {
        if (value.kind == '{' && child.name == name) {
            if (match) throw std::runtime_error("Multiple fields match; use a dotted path: " + std::string(name));
            match = &child;
        }
        find_field(child, name, match);
    }
}

std::string extract_field(std::string_view response, std::string_view path) {
    const auto envelope = JsonParser(response).parse();
    const auto* ok = member(envelope, "ok");
    if (!ok || ok->kind != 't') throw std::runtime_error("Expected a successful client response (ok: true)");
    const auto* body = member(envelope, "body");
    if (!body) throw std::runtime_error("Response has no body; Base64 bodies are not supported");
    const std::string document = body->kind == '"' ? body->text :
        std::string(response.substr(body->begin, body->end - body->begin));
    const auto root = JsonParser(document).parse();
    const JsonValue* selected = &root;
    if (path.empty()) throw std::runtime_error("Field path must not be empty");
    if (path.find('.') == std::string_view::npos) {
        selected = nullptr;
        find_field(root, path, selected);
        if (!selected) throw std::runtime_error("Field not found: " + std::string(path));
        return document.substr(selected->begin, selected->end - selected->begin);
    }
    for (;;) {
        const auto separator = path.find('.');
        const auto segment = path.substr(0, separator);
        if (segment.empty()) throw std::runtime_error("Field path contains an empty segment");
        if (selected->kind == '[') {
            std::size_t index = 0;
            const auto result = std::from_chars(segment.data(), segment.data() + segment.size(), index);
            if (result.ec != std::errc{} || result.ptr != segment.data() + segment.size() ||
                index >= selected->children.size()) throw std::runtime_error("Invalid array index: " + std::string(segment));
            selected = &selected->children[index];
        } else {
            selected = member(*selected, segment);
            if (!selected) throw std::runtime_error("Field not found: " + std::string(segment));
        }
        if (separator == std::string_view::npos) break;
        path.remove_prefix(separator + 1);
    }
    return document.substr(selected->begin, selected->end - selected->begin);
}

int run(int argc, char** argv, std::istream& input, std::ostream& output, std::ostream& errors) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            output << "Usage: response_reader --field <name.or.nested.path> < response.json\n"
                      "Reads the HTTP client's JSON response from stdin. Outputs the field as JSON.\n"
                      "A single field name is searched recursively; multiple matches are an error.\n"
                      "Array indices are supported: --field data.items.0.name\n";
            return 0;
        }
        if (argc != 3 || std::string_view(argv[1]) != "--field") {
            throw std::runtime_error("Usage: response_reader --field <name.or.nested.path>");
        }
        std::string response;
        char buffer[8192];
        constexpr std::size_t kMaxInput = 128U * 1024U * 1024U;
        while (input.read(buffer, sizeof(buffer)) || input.gcount() != 0) {
            const auto count = static_cast<std::size_t>(input.gcount());
            if (count > kMaxInput - response.size()) throw std::runtime_error("Input exceeds 128 MiB");
            response.append(buffer, count);
        }
        if (input.bad() || (!input.eof() && input.fail())) throw std::runtime_error("Cannot read stdin");
        // Windows PowerShell may prefix redirected UTF-8 files with a BOM.
        if (response.starts_with("\xef\xbb\xbf")) response.erase(0, 3);
        output << extract_field(response, argv[2]) << '\n';
        return 0;
    } catch (const std::exception& error) {
        errors << "response_reader: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace response_reader

#ifndef RESPONSE_READER_NO_MAIN
int main(int argc, char** argv) {
    return response_reader::run(argc, argv, std::cin, std::cout, std::cerr);
}
#endif
