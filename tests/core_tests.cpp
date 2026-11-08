#include "mtls/core.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    require(mtls::json_escape("a\n\"b\\") == "\"a\\n\\\"b\\\\\"", "JSON escaping");
    require(mtls::is_valid_utf8({0x48, 0xc3, 0xa9}), "valid UTF-8");
    require(!mtls::is_valid_utf8({0xc0, 0x80}), "overlong UTF-8 rejected");
    require(!mtls::is_valid_utf8({0xed, 0xa0, 0x80}), "surrogate UTF-8 rejected");
    require(mtls::base64_encode({0x66, 0x6f, 0x6f}) == "Zm9v", "Base64 full group");
    require(mtls::base64_encode({0x66}) == "Zg==", "Base64 padding");

    const auto headers = mtls::parse_raw_headers(
        "HTTP/1.1 200 OK\r\nSet-Cookie: a=1\r\nset-cookie: b=2\r\nX-Test: yes\r\n\r\n");
    require(headers.size() == 3, "header count");
    require(headers[0].first == "set-cookie" && headers[0].second == "a=1", "header parsing");
    require(mtls::headers_json(headers) ==
                "{\"set-cookie\":[\"a=1\",\"b=2\"],\"x-test\":[\"yes\"]}",
            "repeated header preservation");

    const auto binary = mtls::response_json(200, "OK", {}, {0xff});
    require(binary.find("\"bodyBase64\":\"/w==\"") != std::string::npos,
            "binary response encoding");
    const auto error = mtls::error_json("send", 5, "bad\nvalue");
    require(error ==
                "{\"ok\":false,\"error\":{\"stage\":\"send\",\"code\":5,\"message\":\"bad\\nvalue\"}}",
            "error JSON");

    std::cout << "All core tests passed\n";
    return 0;
}
