// Keep these tests in the same translation unit as the private CLI helpers.
#define wmain client_wmain
#include "../src/main.cpp"
#undef wmain

#include <initializer_list>
#include <sstream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Options parse(std::initializer_list<std::wstring> arguments) {
    std::vector<std::wstring> values{L"mtls_client", L"--method", L"GET",
                                     L"--url", L"https://localhost/"};
    values.insert(values.end(), arguments.begin(), arguments.end());
    std::vector<wchar_t*> argv;
    for (auto& value : values) argv.push_back(value.data());
    return parse_options(static_cast<int>(argv.size()), argv.data());
}

struct TestFile {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        (L"mtls-options-" + std::to_wstring(GetCurrentProcessId()) + L".pfx");
    TestFile() {
        std::ofstream output(path, std::ios::binary);
        output << "invalid PFX";
        if (!output) throw std::runtime_error("Cannot create test fixture");
    }
    ~TestFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

void test_request_security() {
    // WinHTTP handles can be configured and inspected without sending a request.
    InternetHandle session(WinHttpOpen(L"mtls-options-test", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    require(session != nullptr, "Create offline WinHTTP session");
    InternetHandle connection(WinHttpConnect(session.get(), L"localhost", 443, 0));
    require(connection != nullptr, "Create offline connection handle");
    for (bool secure : {false, true}) {
        InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", L"/", nullptr,
                                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                  secure ? WINHTTP_FLAG_SECURE : 0));
        require(request != nullptr, "Create offline request handle");
        configure_request_security(request.get(), secure);
        DWORD flags = 0;
        DWORD size = sizeof(flags);
        require(WinHttpQueryOption(request.get(), WINHTTP_OPTION_SECURITY_FLAGS, &flags, &size),
                "Read configured request security flags");
        const DWORD ignored_errors = SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                     SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                     SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        require((flags & ignored_errors) == (secure ? ignored_errors : 0),
                "HTTPS must relax all four certificate checks; HTTP must not set TLS flags");
        const auto callback = WinHttpSetStatusCallback(request.get(), nullptr, 0, 0);
        require(callback != WINHTTP_INVALID_STATUS_CALLBACK && callback != nullptr,
                "TLS diagnostics callback must be registered");
        DWORD failures = WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID |
                         WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID |
                         WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA |
                         WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED |
                         WINHTTP_CALLBACK_STATUS_FLAG_CERT_WRONG_USAGE;
        std::ostringstream diagnostics;
        auto* original = std::cerr.rdbuf(diagnostics.rdbuf());
        callback(request.get(), 0, WINHTTP_CALLBACK_STATUS_SECURE_FAILURE,
                 &failures, sizeof(failures));
        std::cerr.rdbuf(original);
        require(diagnostics.str() == "CERT_CN_INVALID\nCERT_DATE_INVALID\nINVALID_CA\n"
                                     "CERT_REV_FAILED\nCERT_WRONG_USAGE\n",
                "TLS failures must report each diagnostic to stderr");
    }
}

}  // namespace

int main() {
    try {
        test_request_security();
        TestFile file;
        const auto http = parse_url(L"http://localhost:8080/api/hello");
        require(!http.secure && http.port == 8080 && http.host == L"localhost" &&
                    http.target == L"/api/hello", "HTTP URL must retain host, port and path");
        const auto https = parse_url(L"https://localhost/api/hello?name=test");
        require(https.secure && https.port == 443 && https.target == L"/api/hello?name=test",
                "HTTPS URL must retain TLS and query string");
        bool invalid_scheme = false;
        try {
            parse_url(L"ftp://localhost/api/hello");
        } catch (const AppError&) {
            invalid_scheme = true;
        }
        require(invalid_scheme, "Unsupported URL schemes must fail");
        auto http_options = parse({L"--pfx", file.path.wstring(), L"--pfx-password", L"secret"});
        http_options.url = L"http://localhost:8080/api/hello";
        bool http_certificate_rejected = false;
        try {
            execute(http_options);
        } catch (const AppError& error) {
            http_certificate_rejected = error.stage() == "certificate";
        }
        require(http_certificate_rejected, "HTTP must reject client certificates before connecting");
        const auto missing = file.path.wstring() + L".missing";
        require(!std::filesystem::exists(missing), "Missing fixture must not exist");
        require(parse({}).pfx.empty(), "Omitted PFX must not prompt for a password");
        require(parse({L"--pfx", missing}).pfx.empty(),
                "Missing PFX must not prompt for a password");
        _wputenv_s(L"MTLS_OPTIONS_TEST_PASSWORD", L"");
        require(parse({L"--pfx", missing, L"--pfx-password-env",
                       L"MTLS_OPTIONS_TEST_PASSWORD"}).pfx.empty(),
                "Missing PFX must not require a password environment variable");
        require(parse({L"--pfx-password-env", L"MTLS_OPTIONS_TEST_PASSWORD"}).pfx.empty(),
                "Omitted PFX must ignore password environment variable");
        auto options = parse({L"--pfx", file.path.wstring(), L"--pfx-password", L"secret"});
        require(options.pfx == file.path && options.password == L"secret",
                "Existing PFX must retain its path and password");
        bool rejected = false;
        try {
            load_identity(options.pfx, options.password);
        } catch (const AppError& error) {
            rejected = error.stage() == "pfx";
        }
        require(rejected, "Existing invalid PFX must fail instead of falling back");
        rejected = false;
        try {
            parse({L"--pfx", file.path.wstring(), L"--pfx-password-env",
                   L"MTLS_OPTIONS_TEST_PASSWORD"});
        } catch (const AppError& error) {
            rejected = error.stage() == "arguments";
        }
        require(rejected, "Existing PFX must require the configured password variable");
        std::cout << "All options tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
