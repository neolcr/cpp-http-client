#include "mtls/core.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace mtls {
namespace {

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}  // namespace

std::string json_escape(std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string output;
    output.reserve(value.size() + 2);
    output.push_back('"');
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (ch < 0x20) {
                    output += "\\u00";
                    output.push_back(kHex[ch >> 4]);
                    output.push_back(kHex[ch & 0x0f]);
                } else {
                    output.push_back(static_cast<char>(ch));
                }
        }
    }
    output.push_back('"');
    return output;
}

bool is_valid_utf8(const Bytes& value) {
    std::size_t index = 0;
    while (index < value.size()) {
        const auto first = value[index];
        std::size_t continuation = 0;
        std::uint32_t code_point = 0;
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        if ((first & 0xe0) == 0xc0) {
            continuation = 1;
            code_point = first & 0x1f;
            if (code_point < 2) return false;
        } else if ((first & 0xf0) == 0xe0) {
            continuation = 2;
            code_point = first & 0x0f;
        } else if ((first & 0xf8) == 0xf0) {
            continuation = 3;
            code_point = first & 0x07;
        } else {
            return false;
        }
        if (index + continuation >= value.size()) return false;
        for (std::size_t offset = 1; offset <= continuation; ++offset) {
            const auto next = value[index + offset];
            if ((next & 0xc0) != 0x80) return false;
            code_point = (code_point << 6) | (next & 0x3f);
        }
        if ((continuation == 2 && code_point < 0x800) ||
            (continuation == 3 && code_point < 0x10000) ||
            (code_point >= 0xd800 && code_point <= 0xdfff) || code_point > 0x10ffff) {
            return false;
        }
        index += continuation + 1;
    }
    return true;
}

std::string base64_encode(const Bytes& value) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((value.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < value.size(); index += 3) {
        const std::uint32_t first = value[index];
        const std::uint32_t second = index + 1 < value.size() ? value[index + 1] : 0;
        const std::uint32_t third = index + 2 < value.size() ? value[index + 2] : 0;
        const std::uint32_t group = (first << 16) | (second << 8) | third;
        output.push_back(kAlphabet[(group >> 18) & 0x3f]);
        output.push_back(kAlphabet[(group >> 12) & 0x3f]);
        output.push_back(index + 1 < value.size() ? kAlphabet[(group >> 6) & 0x3f] : '=');
        output.push_back(index + 2 < value.size() ? kAlphabet[group & 0x3f] : '=');
    }
    return output;
}

Headers parse_raw_headers(std::string_view raw_headers) {
    Headers result;
    std::size_t position = 0;
    bool first_line = true;
    while (position < raw_headers.size()) {
        const auto end = raw_headers.find("\r\n", position);
        const auto line = raw_headers.substr(
            position, end == std::string_view::npos ? raw_headers.size() - position : end - position);
        position = end == std::string_view::npos ? raw_headers.size() : end + 2;
        if (line.empty()) break;
        if (first_line) {
            first_line = false;
            if (line.starts_with("HTTP/")) continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) continue;
        result.emplace_back(lowercase(trim(line.substr(0, colon))), trim(line.substr(colon + 1)));
    }
    return result;
}

std::string headers_json(const Headers& headers) {
    std::map<std::string, std::vector<std::string>> grouped;
    for (const auto& [name, value] : headers) grouped[lowercase(name)].push_back(value);
    std::ostringstream output;
    output << '{';
    bool first_name = true;
    for (const auto& [name, values] : grouped) {
        if (!first_name) output << ',';
        first_name = false;
        output << json_escape(name) << ":[";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0) output << ',';
            output << json_escape(values[index]);
        }
        output << ']';
    }
    output << '}';
    return output.str();
}

std::string response_json(unsigned long status,
                          std::string_view reason,
                          const Headers& headers,
                          const Bytes& body) {
    std::ostringstream output;
    output << "{\"ok\":true,\"status\":" << status << ",\"reason\":" << json_escape(reason)
           << ",\"headers\":" << headers_json(headers);
    if (is_valid_utf8(body)) {
        output << ",\"body\":" << json_escape(std::string_view(
            reinterpret_cast<const char*>(body.data()), body.size()));
    } else {
        output << ",\"bodyBase64\":" << json_escape(base64_encode(body))
               << ",\"bodyEncoding\":\"base64\"";
    }
    output << '}';
    return output.str();
}

std::string error_json(std::string_view stage,
                       unsigned long code,
                       std::string_view message) {
    std::ostringstream output;
    output << "{\"ok\":false,\"error\":{\"stage\":" << json_escape(stage)
           << ",\"code\":" << code << ",\"message\":" << json_escape(message) << "}}";
    return output.str();
}

}  // namespace mtls

