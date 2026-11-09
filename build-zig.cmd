@echo off
setlocal

where zig >nul 2>nul
if errorlevel 1 (
    echo ERROR: zig.exe was not found on PATH. 1>&2
    exit /b 1
)

set "ZIG_TARGET=x86_64-windows-gnu"
if not "%~1"=="" set "ZIG_TARGET=%~1"

if not exist "zig-out\bin" mkdir "zig-out\bin"

echo Building mtls_client.exe for %ZIG_TARGET%...
zig c++ ^
    -target %ZIG_TARGET% ^
    -std=c++20 -O2 -Wall -Wextra -Wpedantic -municode ^
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
    -Iinclude ^
    src\main.cpp src\core.cpp ^
    -lwinhttp -lcrypt32 -lncrypt -ladvapi32 ^
    -o zig-out\bin\mtls_client.exe
if errorlevel 1 exit /b 1

echo Building and running portable core tests...
zig c++ ^
    -target %ZIG_TARGET% ^
    -std=c++20 -O2 -Wall -Wextra -Wpedantic ^
    -Iinclude ^
    src\core.cpp tests\core_tests.cpp ^
    -o zig-out\bin\core_tests.exe
if errorlevel 1 exit /b 1

zig-out\bin\core_tests.exe
if errorlevel 1 exit /b 1

echo Building and running Windows options tests...
zig c++ ^
    -target %ZIG_TARGET% ^
    -std=c++20 -O2 -Wall -Wextra -Wpedantic ^
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
    -Iinclude ^
    src\core.cpp tests\options_test.cpp ^
    -lwinhttp -lcrypt32 -lncrypt -ladvapi32 ^
    -o zig-out\bin\options_test.exe
if errorlevel 1 exit /b 1

zig-out\bin\options_test.exe
if errorlevel 1 exit /b 1

echo Built zig-out\bin\mtls_client.exe successfully.
echo Building response_reader.exe...
zig c++ ^
    -target %ZIG_TARGET% ^
    -std=c++20 -O2 -Wall -Wextra -Wpedantic ^
    response_reader\response_reader.cpp ^
    -o zig-out\bin\response_reader.exe
if errorlevel 1 exit /b 1

echo Building and running response reader tests...
zig c++ ^
    -target %ZIG_TARGET% ^
    -std=c++20 -O2 -Wall -Wextra -Wpedantic ^
    tests\response_reader_test.cpp ^
    -o zig-out\bin\response_reader_test.exe
if errorlevel 1 exit /b 1

zig-out\bin\response_reader_test.exe
if errorlevel 1 exit /b 1
endlocal
