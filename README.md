# Jello VM

The **Jello** virtual machine: a C11 bytecode interpreter that loads and runs compiled Jello modules (`.jlo` files). It implements the runtime in `src/`, exposes a small public API via `src/include/jello.h`, and ships a command-line runner for local development and testing.

## Requirements

- **CMake** 3.16 or newer  
- A **C11** compiler (GCC, Clang, or compatible Apple Clang)

## Build

Configure and build out of tree (recommended):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Artifacts:

- **CLI:** `build/bin/jellovm`
- **Static library:** `build/lib/libjellovm.a`

Run a module:

```bash
./build/bin/jellovm path/to/module.jlo
```

Optional profiling timestamps (monotonic clock where available):

```bash
JELLO_PROFILE=1 ./build/bin/jellovm path/to/module.jlo
```

### CMake options (selection)

| Option | Default | Notes |
|--------|---------|--------|
| `JELLOVM_BUILD_TESTS` | `ON` | When `ON`, expects a sibling **`ctest`** checkout next to this repo (`../ctest`). Set `OFF` to build the VM only. |
| `JELLOVM_USE_COMPUTED_GOTO` | `ON` in Release | Faster opcode dispatch on GCC/Clang; ignored on other compilers. |
| `JELLOVM_ENABLE_LTO` | `ON` in Release | Link-time optimization when supported. |
| `JELLOVM_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |
| `JELLOVM_ENABLE_ASAN` / `JELLOVM_ENABLE_UBSAN` | `OFF` | Sanitizers (GCC/Clang). |

Example, VM only (no test tree):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJELLOVM_BUILD_TESTS=OFF
cmake --build build
```

`compile_commands.json` is generated in the build directory when using CMake’s default behavior (useful for clangd and other tools).

## Layout

| Path | Role |
|------|------|
| `src/include/jello.h` | Public C API |
| `src/include/jello/internal/` | VM-internal headers (not for embedders) |
| `src/vm/`, `src/bytecode/`, `src/types/`, `src/gc.c` | Interpreter, loader, and runtime |
| `cmake/` | Shared CMake modules (warnings, sanitizers) |

## License

See the BSD-style license headers in the source files (copyright Jahred Love).
