#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mtls {

using Bytes = std::vector<std::uint8_t>;
using Headers = std::vector<std::pair<std::string, std::string>>;

std::string json_escape(std::string_view value);
bool is_valid_utf8(const Bytes& value);
std::string base64_encode(const Bytes& value);
Headers parse_raw_headers(std::string_view raw_headers);
std::string headers_json(const Headers& headers);
std::string response_json(unsigned long status,
                          std::string_view reason,
                          const Headers& headers,
                          const Bytes& body);
std::string error_json(std::string_view stage,
                       unsigned long code,
                       std::string_view message);

}  // namespace mtls

