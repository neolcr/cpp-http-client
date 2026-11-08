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

## Usage

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

- `--method <verb>` and `--url <https-url>` are required.
- `--header "Name: value"` may be repeated.
- `--body <UTF-8 text>` and `--body-file <path>` are mutually exclusive.
- `--pfx <path>` is required. The PFX must contain exactly one currently valid client-auth certificate with an accessible private key.
- Supply the password with `--pfx-password-env <name>` (recommended), `--pfx-password <value>`, or a hidden interactive prompt.
- `--max-response-bytes <count>` changes the default 16 MiB response limit.

Stdout contains exactly one JSON object. Text responses use `body`; non-UTF-8 responses use `bodyBase64` and `bodyEncoding`. HTTP error statuses are valid responses with `ok: true`; transport, certificate, argument, and size failures return `ok: false` and process exit code 1.

## Security behavior

Windows performs normal server-chain and hostname validation. The client does not install trust anchors, ignore certificate errors, or follow redirects. Import uses a temporary PFX store and does not persist the private key. Password command-line arguments are supported for compatibility but may be visible to other local processes; prefer an environment variable or the prompt. Do not commit certificates or passwords.
