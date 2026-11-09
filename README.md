# Windows C++ mTLS Client

`mtls_client` is a native Windows HTTPS command-line client with mutual TLS. It uses only the C++ standard library and Windows SDK APIs: WinHTTP, CryptoAPI, and NCrypt. No third-party packages are required.

## Build on Windows with Zig

Install Zig, ensure `zig.exe` is on `PATH`, and run from `cmd.exe` or PowerShell:

```bat
build-zig.cmd
```

This invokes `zig c++`, targets `x86_64-windows-gnu`, builds the client and core tests, and runs the tests. It uses Zig's bundled C++ and MinGW-w64 environment, so Visual Studio is not required. The executable is written to `zig-out\bin\mtls_client.exe`.

An alternate Windows GNU target can be supplied as the first argument, for example:

```bat
build-zig.cmd aarch64-windows-gnu
```

Cross-target test executables cannot necessarily run on the build machine; for that case, run the first `zig c++` command from `build-zig.cmd` directly or test on a matching Windows host.

## Build on Windows with MSVC

Open an **x64 Native Tools Command Prompt for Visual Studio 2022**:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is normally written to `build\Release\mtls_client.exe`. Both build paths link only Windows system libraries: `winhttp`, `crypt32`, `ncrypt`, and `advapi32`.

## Single-file version

`standalone/mtls_client.cpp` contains the complete application in one C++ file,
including HTTP/HTTPS support and optional PFX authentication. You can copy it
to another directory without any project headers or sources. It requires Windows
and a C++20 compiler. This is a snapshot; future changes to `src/` or `include/`
must also be applied to the single-file version.

From the project root, build it with Zig:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
zig c++ -target x86_64-windows-gnu -std=c++20 -O2 -Wall -Wextra -Wpedantic -municode standalone/mtls_client.cpp -lwinhttp -lcrypt32 -lncrypt -ladvapi32 -o build/mtls_client_standalone.exe
.\build\mtls_client_standalone.exe --method GET --url "http://localhost:8080/api/hello"
```

Or, from an MSVC developer prompt:

```bat
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 /utf-8 standalone\mtls_client.cpp /Fo:build\mtls_client_standalone.obj /Fe:build\mtls_client_standalone.exe /link winhttp.lib crypt32.lib ncrypt.lib advapi32.lib
```

## Usage

To extract a field such as `productType` from the JSON inside a response's
`body`, use the separate [standalone response reader](response_reader/README.md).
It accepts the client's stdout through a pipe and takes the field name as a
command-line argument.

```bat
set MTLS_PFX_PASSWORD=change-me
zig-out\bin\mtls_client.exe ^
  --method POST ^
  --url "https://api.example.com/v1/items?dryRun=true" ^
  --header "Content-Type: application/json" ^
  --header "Accept: application/json" ^
  --body "{\"name\":\"sample\"}" ^
  --pfx "C:\certificates\client.pfx" ^
  --pfx-password-env MTLS_PFX_PASSWORD
```

Arguments:

- `--method <verb>` and `--url <http-or-https-url>` are required. Include the `http://` or `https://` scheme.
- `--header "Name: value"` may be repeated.
- `--body <UTF-8 text>` and `--body-file <path>` are mutually exclusive.
- `--pfx <path>` is optional. If omitted or the file does not exist, the request proceeds without a client certificate. Existing PFX files must contain exactly one certificate with an accessible private key. The client does not filter certificates by validity dates or client-auth usage; invalid files, incorrect passwords, and file access errors still fail.
- When a PFX file exists, supply the password with `--pfx-password-env <name>` (recommended), `--pfx-password <value>`, or a hidden interactive prompt. Password options are ignored when no PFX is used.
- `--max-response-bytes <count>` changes the default 16 MiB response limit.

Stdout contains exactly one JSON object. Text responses use `body`; non-UTF-8 responses use `bodyBase64` and `bodyEncoding`. HTTP error statuses are valid responses with `ok: true`; transport, certificate, argument, and size failures return `ok: false` and process exit code 1.

For HTTPS without a client certificate:

```bat
zig-out\bin\mtls_client.exe --method GET --url "https://api.example.com/"
```

Servers that require mutual TLS still require a valid client certificate.

For a local HTTP server:

```powershell
.\zig-out\bin\mtls_client.exe --method GET --url "http://localhost:8080/api/hello"
```

HTTP does not use TLS. An existing PFX may only be used with an HTTPS URL.

## Security behavior

Both versions ignore HTTPS server certificate hostname, unknown-authority, date, and usage errors on every HTTPS request, with or without a PFX. This relaxes server authentication; other TLS failures can still prevent a connection. Redirects remain disabled.

PFX import uses `CRYPT_USER_KEYSET` without `PKCS12_NO_PERSIST_KEY`, allowing imported private keys to persist under the current Windows user. Closing the imported certificate store does not explicitly delete these keys.

TLS failure diagnostics are written to stderr: `CERT_CN_INVALID`, `CERT_DATE_INVALID`, `INVALID_CA`, `CERT_REV_FAILED`, and `CERT_WRONG_USAGE`, when reported by WinHTTP. Security flags are also printed if reading the HTTP status code fails. Stdout remains the JSON response or error.

Password command-line arguments are supported for compatibility but may be visible to other local processes; prefer an environment variable or the prompt. Do not commit certificates or passwords.
