#include "mtls/core.hpp"

#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <ncrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class AppError : public std::runtime_error {
public:
    AppError(std::string stage, unsigned long code, std::string message)
        : std::runtime_error(std::move(message)), stage_(std::move(stage)), code_(code) {}

    const std::string& stage() const noexcept { return stage_; }
    unsigned long code() const noexcept { return code_; }

private:
    std::string stage_;
    unsigned long code_;
};

std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) throw AppError("encoding", GetLastError(), "Cannot encode text as UTF-8");
    std::string output(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), output.data(), required,
                            nullptr, nullptr) != required) {
        throw AppError("encoding", GetLastError(), "Cannot encode text as UTF-8");
    }
    return output;
}

std::string windows_message(unsigned long code) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                           FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, code, 0,
                                       reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (count == 0 || buffer == nullptr) return "Windows error " + std::to_string(code);
    std::wstring message(buffer, count);
    LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' ||
                                 message.back() == L' ')) {
        message.pop_back();
    }
    return utf8(message);
}

[[noreturn]] void fail_windows(std::string stage, std::string_view action) {
    const auto code = GetLastError();
    throw AppError(std::move(stage), code,
                   std::string(action) + ": " + windows_message(code));
}

struct InternetCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr) WinHttpCloseHandle(handle);
    }
};
using InternetHandle = std::unique_ptr<void, InternetCloser>;

struct StoreCloser {
    void operator()(void* store) const noexcept {
        if (store != nullptr) CertCloseStore(store, 0);
    }
};
using CertStore = std::unique_ptr<void, StoreCloser>;

struct CertCloser {
    void operator()(const CERT_CONTEXT* certificate) const noexcept {
        if (certificate != nullptr) CertFreeCertificateContext(certificate);
    }
};
using Certificate = std::unique_ptr<const CERT_CONTEXT, CertCloser>;

struct Options {
    std::wstring method;
    std::wstring url;
    std::vector<std::wstring> headers;
    mtls::Bytes body;
    std::filesystem::path pfx;
    std::wstring password;
    std::size_t max_response = 16U * 1024U * 1024U;
};

std::wstring required_value(int& index, int argc, wchar_t** argv, std::wstring_view option) {
    if (index + 1 >= argc) {
        throw AppError("arguments", 2, "Missing value for " + utf8(option));
    }
    return argv[++index];
}

mtls::Bytes read_file(const std::filesystem::path& path, std::string_view stage) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw AppError(std::string(stage), 3, "Cannot open file: " + utf8(path.wstring()));
    input.seekg(0, std::ios::end);
    const std::streamsize length = input.tellg();
    if (length < 0) throw AppError(std::string(stage), 3, "Cannot determine file size");
    if (static_cast<unsigned long long>(length) >
        static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw AppError(std::string(stage), 3, "File is too large");
    }
    mtls::Bytes bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        throw AppError(std::string(stage), 3, "Cannot read file");
    }
    return bytes;
}

std::wstring prompt_password() {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD original_mode = 0;
    if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &original_mode)) {
        throw AppError("arguments", 2,
                       "No PFX password supplied and stdin is not an interactive console");
    }
    std::wcerr << L"PFX password: ";
    if (!SetConsoleMode(input, original_mode & ~ENABLE_ECHO_INPUT)) fail_windows("arguments", "Disable console echo");
    std::wstring password;
    std::getline(std::wcin, password);
    SetConsoleMode(input, original_mode);
    std::wcerr << L'\n';
    return password;
}

bool valid_method(std::wstring_view value) {
    static constexpr std::wstring_view kExtra = L"!#$%&'*+-.^_`|~";
    return !value.empty() && std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
               (ch >= L'0' && ch <= L'9') || kExtra.find(ch) != std::wstring_view::npos;
    });
}

Options parse_options(int argc, wchar_t** argv) {
    Options options;
    std::filesystem::path body_file;
    bool has_body = false;
    bool has_password = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--method") {
            options.method = required_value(index, argc, argv, argument);
        } else if (argument == L"--url") {
            options.url = required_value(index, argc, argv, argument);
        } else if (argument == L"--header") {
            auto header = required_value(index, argc, argv, argument);
            if (header.find(L'\r') != std::wstring::npos || header.find(L'\n') != std::wstring::npos ||
                header.find(L':') == std::wstring::npos) {
                throw AppError("arguments", 2, "Invalid header syntax");
            }
            options.headers.push_back(std::move(header));
        } else if (argument == L"--body") {
            if (has_body) throw AppError("arguments", 2, "Use only one body option");
            const auto value = required_value(index, argc, argv, argument);
            const auto encoded = utf8(value);
            options.body.assign(encoded.begin(), encoded.end());
            has_body = true;
        } else if (argument == L"--body-file") {
            if (has_body) throw AppError("arguments", 2, "Use only one body option");
            body_file = required_value(index, argc, argv, argument);
            has_body = true;
        } else if (argument == L"--pfx") {
            options.pfx = required_value(index, argc, argv, argument);
        } else if (argument == L"--pfx-password") {
            if (has_password) throw AppError("arguments", 2, "Use only one password option");
            options.password = required_value(index, argc, argv, argument);
            has_password = true;
        } else if (argument == L"--pfx-password-env") {
            if (has_password) throw AppError("arguments", 2, "Use only one password option");
            const auto name = required_value(index, argc, argv, argument);
            const wchar_t* value = _wgetenv(name.c_str());
            if (value == nullptr) throw AppError("arguments", 2, "Password environment variable is not set");
            options.password = value;
            has_password = true;
        } else if (argument == L"--max-response-bytes") {
            const auto text = required_value(index, argc, argv, argument);
            try {
                std::size_t consumed = 0;
                const auto parsed = std::stoull(text, &consumed);
                if (consumed != text.size() || parsed == 0 ||
                    parsed > std::numeric_limits<std::size_t>::max()) throw std::out_of_range("size");
                options.max_response = static_cast<std::size_t>(parsed);
            } catch (...) {
                throw AppError("arguments", 2, "Invalid --max-response-bytes value");
            }
        } else {
            throw AppError("arguments", 2, "Unknown option: " + utf8(argument));
        }
    }
    if (!valid_method(options.method)) throw AppError("arguments", 2, "A valid --method is required");
    if (options.url.empty()) throw AppError("arguments", 2, "--url is required");
    if (options.pfx.empty()) throw AppError("arguments", 2, "--pfx is required");
    if (!body_file.empty()) options.body = read_file(body_file, "body");
    if (!has_password) options.password = prompt_password();
    return options;
}

struct ParsedUrl {
    std::wstring host;
    std::wstring target;
    INTERNET_PORT port = 0;
};

ParsedUrl parse_url(const std::wstring& url) {
    if (url.size() > std::numeric_limits<DWORD>::max()) {
        throw AppError("url", 2, "URL is too long");
    }
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        fail_windows("url", "Parse URL");
    }
    if (components.nScheme != INTERNET_SCHEME_HTTPS || components.lpszHostName == nullptr ||
        components.dwHostNameLength == 0) {
        throw AppError("url", 2, "URL must be an absolute https:// URL with a host");
    }
    if (url.find(L'#') != std::wstring::npos) {
        throw AppError("url", 2, "URL fragments are not sent in HTTP requests and are not accepted");
    }
    ParsedUrl parsed;
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.port = components.nPort;
    if (components.lpszUrlPath != nullptr && components.dwUrlPathLength != 0) {
        parsed.target.assign(components.lpszUrlPath, components.dwUrlPathLength);
    } else {
        parsed.target = L"/";
    }
    if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength != 0) {
        parsed.target.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    return parsed;
}

bool supports_client_auth(PCCERT_CONTEXT certificate) {
    DWORD size = 0;
    if (!CertGetEnhancedKeyUsage(certificate, 0, nullptr, &size)) {
        return GetLastError() == CRYPT_E_NOT_FOUND;
    }
    std::vector<std::uint8_t> buffer(size);
    auto* usage = reinterpret_cast<PCERT_ENHKEY_USAGE>(buffer.data());
    if (!CertGetEnhancedKeyUsage(certificate, 0, usage, &size)) return false;
    if (usage->cUsageIdentifier == 0) return true;
    for (DWORD index = 0; index < usage->cUsageIdentifier; ++index) {
        if (std::string_view(usage->rgpszUsageIdentifier[index]) == szOID_PKIX_KP_CLIENT_AUTH) return true;
    }
    return false;
}

bool has_private_key(PCCERT_CONTEXT certificate) {
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    DWORD key_spec = 0;
    BOOL must_free = FALSE;
    if (!CryptAcquireCertificatePrivateKey(certificate, CRYPT_ACQUIRE_SILENT_FLAG,
                                            nullptr, &key, &key_spec, &must_free)) {
        return false;
    }
    if (must_free) {
        if (key_spec == CERT_NCRYPT_KEY_SPEC) {
            NCryptFreeObject(key);
        } else {
            CryptReleaseContext(static_cast<HCRYPTPROV>(key), 0);
        }
    }
    return true;
}

struct Identity {
    CertStore store;
    Certificate certificate;
};

class PasswordWiper {
public:
    explicit PasswordWiper(std::wstring& password) : password_(password) {}
    ~PasswordWiper() {
        if (!password_.empty()) SecureZeroMemory(password_.data(), password_.size() * sizeof(wchar_t));
    }
    PasswordWiper(const PasswordWiper&) = delete;
    PasswordWiper& operator=(const PasswordWiper&) = delete;

private:
    std::wstring& password_;
};

Identity load_identity(const std::filesystem::path& path, std::wstring& password) {
    PasswordWiper wipe(password);
    auto bytes = read_file(path, "pfx");
    if (bytes.size() > std::numeric_limits<DWORD>::max()) throw AppError("pfx", 4, "PFX file is too large");
    CRYPT_DATA_BLOB blob{static_cast<DWORD>(bytes.size()), bytes.data()};
    if (!PFXIsPFXBlob(&blob)) throw AppError("pfx", 4, "File is not a valid PFX/PKCS#12 blob");
    CertStore store(PFXImportCertStore(&blob, password.c_str(),
                                       CRYPT_USER_KEYSET | PKCS12_NO_PERSIST_KEY));
    if (!store) fail_windows("pfx", "Import PFX");

    std::vector<Certificate> matches;
    PCCERT_CONTEXT current = nullptr;
    while ((current = CertEnumCertificatesInStore(store.get(), current)) != nullptr) {
        if (CertVerifyTimeValidity(nullptr, current->pCertInfo) == 0 &&
            supports_client_auth(current) && has_private_key(current)) {
            matches.emplace_back(CertDuplicateCertificateContext(current));
        }
    }
    if (matches.empty()) throw AppError("certificate", 4, "PFX has no valid client-auth certificate with a private key");
    if (matches.size() != 1) throw AppError("certificate", 4, "PFX has multiple suitable client certificates; selection is ambiguous");
    auto selected = std::move(matches.front());
    return {std::move(store), std::move(selected)};
}

std::wstring query_header_text(HINTERNET request, DWORD query) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) fail_windows("response_headers", "Size response header");
    std::vector<wchar_t> buffer(size / sizeof(wchar_t));
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        fail_windows("response_headers", "Read response header");
    }
    const auto length = size / sizeof(wchar_t);
    return std::wstring(buffer.data(), length != 0 && buffer[length - 1] == L'\0' ? length - 1 : length);
}

std::string execute(Options& options) {
    const auto parsed = parse_url(options.url);
    auto identity = load_identity(options.pfx, options.password);

    InternetHandle session(WinHttpOpen(L"windows-mtls-client/1.0",
                                        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) fail_windows("session", "Open WinHTTP session");
    InternetHandle connection(WinHttpConnect(session.get(), parsed.host.c_str(), parsed.port, 0));
    if (!connection) fail_windows("connect", "Open connection");
    InternetHandle request(WinHttpOpenRequest(connection.get(), options.method.c_str(),
                                               parsed.target.c_str(), nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE));
    if (!request) fail_windows("request", "Create request");

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                          &redirect_policy, sizeof(redirect_policy))) {
        fail_windows("request", "Disable redirects");
    }
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_CLIENT_CERT_CONTEXT,
                          const_cast<CERT_CONTEXT*>(identity.certificate.get()), sizeof(CERT_CONTEXT))) {
        fail_windows("certificate", "Attach client certificate");
    }
    for (const auto& header : options.headers) {
        if (header.size() > std::numeric_limits<DWORD>::max()) {
            throw AppError("request_headers", 2, "Request header is too long");
        }
        if (!WinHttpAddRequestHeaders(request.get(), header.c_str(), static_cast<DWORD>(header.size()),
                                      WINHTTP_ADDREQ_FLAG_ADD)) {
            fail_windows("request_headers", "Add request header");
        }
    }
    if (options.body.size() > std::numeric_limits<DWORD>::max()) {
        throw AppError("body", 3, "Request body exceeds WinHTTP's single-buffer limit");
    }
    const DWORD body_size = static_cast<DWORD>(options.body.size());
    void* body = options.body.empty() ? WINHTTP_NO_REQUEST_DATA
                                      : const_cast<std::uint8_t*>(options.body.data());
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            body, body_size, body_size, 0)) {
        fail_windows("send", "Send request");
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr)) fail_windows("receive", "Receive response");

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                             WINHTTP_NO_HEADER_INDEX)) {
        fail_windows("response_headers", "Read status code");
    }
    const auto reason = utf8(query_header_text(request.get(), WINHTTP_QUERY_STATUS_TEXT));
    const auto raw_headers = utf8(query_header_text(request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF));

    mtls::Bytes response_body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) fail_windows("response_body", "Query response data");
        if (available == 0) break;
        if (available > options.max_response - response_body.size()) {
            throw AppError("response_body", 5, "Response exceeds --max-response-bytes");
        }
        const auto old_size = response_body.size();
        response_body.resize(old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), response_body.data() + old_size, available, &read)) {
            fail_windows("response_body", "Read response data");
        }
        response_body.resize(old_size + read);
        if (read == 0) break;
    }
    return mtls::response_json(status, reason, mtls::parse_raw_headers(raw_headers), response_body);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        auto options = parse_options(argc, argv);
        std::cout << execute(options) << '\n';
        return 0;
    } catch (const AppError& error) {
        std::cout << mtls::error_json(error.stage(), error.code(), error.what()) << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cout << mtls::error_json("internal", 1, error.what()) << '\n';
        return 1;
    }
}
