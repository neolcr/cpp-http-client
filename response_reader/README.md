# Response reader

`response_reader.cpp` is a standalone C++20 application with no external
dependencies. It reads one UTF-8 JSON response from stdin, decodes the JSON in
its `body` string, and searches for the field supplied with `--field`.

For example, given this HTTP client output:

```json
{"ok":true,"status":200,"body":"{\"productType\":\"book\",\"price\":12.5}"}
```

`--field productType` prints:

```json
"book"
```

The output is a JSON value: strings retain quotation marks, and numbers,
booleans, nulls, objects, and arrays retain their types. The app searches field
names recursively through objects and arrays. If a name appears multiple times,
use a dotted path to select one explicitly, such as `data.productType` or
`data.items.0.productType`. Dots separate path segments and cannot be used as
literal characters within field names.

## Build with Zig

From the project root in PowerShell:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
zig c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic response_reader/response_reader.cpp -o build/response_reader.exe
```

You can also copy `response_reader.cpp` into another directory and compile it
there. Unlike the HTTP client, the reader does not depend on Windows APIs.

The project's `build-zig.cmd` also builds the reader and its tests, placing the
executables in `zig-out/bin`. CMake builds the `response_reader` target and
registers `response_reader_test` with CTest.

## Usage

Pipe the HTTP client's output directly to the reader:

```powershell
.\zig-out\bin\mtls_client.exe --method GET --url "http://localhost:8080/api/products/1" |
    .\zig-out\bin\response_reader.exe --field productType
```

Or read a saved response:

```powershell
Get-Content response.json -Raw | .\build\response_reader.exe --field productType
```

From `cmd.exe`, file redirection is also supported:

```bat
build\response_reader.exe --field productType < response.json
```

The reader requires `ok: true` in the outer response. It accepts `body` as a JSON
string or an already parsed JSON value. Missing fields, ambiguous matches,
malformed JSON, duplicate object keys, unsuccessful client responses, and
Base64-only bodies produce an explanation on stderr and exit code 1. Success
returns exit code 0. HTTP status codes are not filtered; an HTTP error response
with `ok: true` can still be examined. Input is limited to 128 MiB and JSON
nesting to 128 levels. `--help` prints usage.
