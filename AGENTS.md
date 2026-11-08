# Repository Guidelines

## Project Structure & Module Organization

This repository is currently an empty C++ project. Keep the root uncluttered as code is introduced. Place public headers in `include/`, implementation files in `src/`, and automated tests in `tests/`. Use `examples/` for small runnable demonstrations and `assets/` only for non-code resources required by the program or tests. Keep generated output in `build/`; do not commit it.

Prefer feature-oriented pairs such as `include/parser.hpp` and `src/parser.cpp`. If the project grows, group related code under matching subdirectories in both trees.

## Build, Test, and Development Commands

No build system is committed yet. When adding the first target, prefer CMake and document any new prerequisites in `README.md`. A conventional local workflow is:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The first command configures an out-of-tree debug build, the second compiles all targets, and the third runs registered tests while showing failure details. Use `cmake --build build --target clean` to clear compiled artifacts without deleting source files.

## Coding Style & Naming Conventions

Target modern, portable C++ and specify the chosen language standard in CMake. Use four spaces, no tabs, and keep one declaration per line. Name files and functions with `snake_case`, types with `PascalCase`, and constants with `kPascalCase`. Prefer RAII, standard-library facilities, and explicit ownership over raw `new`/`delete`.

If formatting is introduced, commit a `.clang-format` file and run `clang-format -i` on changed C++ files. Keep compiler warnings enabled and address warnings rather than suppressing them broadly.

## Testing Guidelines

Add tests with each behavior change or bug fix. Name test files after their subject, for example `tests/parser_test.cpp`, and use descriptive case names that state the expected behavior. Register every test executable with CTest so the command above remains the single test entry point. Avoid tests that depend on network access, execution order, or developer-specific paths.

## Commit & Pull Request Guidelines

There is no existing Git history from which to infer a convention. Use short, imperative commit subjects, optionally with a scope, such as `parser: reject malformed input`. Keep commits focused and include tests with functional changes.

Pull requests should explain the motivation, summarize the implementation, list verification commands, and link relevant issues. Include logs or screenshots only when they clarify user-visible output or failures. Note any compatibility, dependency, or configuration changes explicitly.
