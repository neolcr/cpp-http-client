#define RESPONSE_READER_NO_MAIN
#include "../response_reader/response_reader.cpp"

#include <sstream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void rejects(std::string_view input, std::string_view field) {
    bool rejected = false;
    try {
        response_reader::extract_field(input, field);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Expected invalid input or field to be rejected");
}

}  // namespace

int main() {
    try {
        using response_reader::extract_field;
        const std::string response = R"({"ok":true,"status":200,"body":"{\"productType\":\"book\",\"stock\":42,\"available\":true,\"extra\":null}"})";
        require(extract_field(response, "productType") == R"("book")", "Decode body JSON string and find productType");
        require(extract_field(response, "stock") == "42", "Preserve number type");
        require(extract_field(response, "available") == "true", "Preserve boolean type");
        require(extract_field(response, "extra") == "null", "Distinguish null from missing field");
        const std::string nested = R"({"ok":true,"body":"{\"data\":{\"items\":[{\"productType\":\"book\"}]}}"})";
        require(extract_field(nested, "productType") == R"("book")", "Search nested objects and arrays by field name");
        require(extract_field(nested, "data.items.0.productType") == R"("book")", "Follow explicit nested path and array index");
        require(extract_field(nested, "data.items") == R"([{"productType":"book"}])", "Return entire JSON array value");
        require(extract_field(R"({"ok":true,"body":{"productType":"book"}})", "productType") == R"("book")", "Accept an already parsed body object");
        require(extract_field(R"({"ok":true,"body":"{\"product\u0054ype\":\"line\\nquote\\\"\"}"})", "productType") == R"("line\nquote\"")", "Decode escaped key and preserve escaped value");
        require(extract_field(R"({"ok":true,"body":"{\"productType\":\"\uD83D\uDE00\"}"})", "productType") == "\"\xf0\x9f\x98\x80\"", "Decode Unicode surrogate pair in body envelope");
        rejects(response, "missing");
        rejects(response, "");
        rejects(nested, "data..items");
        rejects(nested, "data.items.9.productType");
        rejects(R"({"ok":true,"body":"{\"a\":{\"productType\":1},\"b\":{\"productType\":2}}"})", "productType");
        rejects(R"({"ok":false,"error":{"message":"Transport failed"}})", "productType");
        rejects(R"({"ok":true,"bodyBase64":"AA=="})", "productType");
        rejects(R"({"ok":true,"body":"plain text"})", "productType");
        rejects(R"({"ok":true,"body":"{\"productType\":1,}"})", "productType");
        rejects(R"({"ok":true,"body":"{\"productType\":01}"})", "productType");
        rejects(R"({"ok":true,"body":"{\"productType\":1,\"productType\":2}"})", "productType");
        rejects(R"({"ok":true,"body":"{}"} trailing)", "productType");
        rejects(R"({"ok":true,"body":"\uD800"})", "productType");
        rejects("", "productType");
        std::string nested_too_deep = R"({"ok":true,"body":)" + std::string(130, '[') + "0" + std::string(130, ']') + "}";
        rejects(nested_too_deep, "productType");

        char program[] = "response_reader";
        char option[] = "--field";
        char field[] = "productType";
        char* argv[] = {program, option, field};
        std::istringstream input("\xef\xbb\xbf" + response);
        std::ostringstream output;
        std::ostringstream errors;
        require(response_reader::run(3, argv, input, output, errors) == 0,
                "CLI accepts UTF-8 BOM and stdin input");
        require(output.str() == "\"book\"\n" && errors.str().empty(), "Successful CLI emits only selected JSON");
        std::istringstream invalid_input("{}");
        output.str("");
        require(response_reader::run(3, argv, invalid_input, output, errors) == 1,
                "CLI returns failure for invalid response");
        require(output.str().empty() && !errors.str().empty(), "CLI errors go to stderr");
        std::cout << "All response reader tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
