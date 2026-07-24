# Jello VM

The **Jello** virtual machine: a C11 bytecode interpreter that loads and runs compiled Jello modules (`.jlo` files). It implements the runtime in `src/`, exposes a small public API via `src/include/jello.h`, and ships a command-line runner for local development and testing.

See the [Jello Compiler](https://github.com/Lazarus404/jello-compiler), written in OCaml, for creating Jello apps.

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

### Native extensions (`.jdll`)

The VM loads `.jdll` shared libraries for `import { … } from lib` when the linked bytecode lists native dependencies. Search order matches the compiler (`jelloc/src/link/jdll.rs`):

1. `{script_dir}/jdll/`, then flat `{script_dir}/`
2. Walk up to 12 ancestors for `jdll/` directories
3. **`JELLO_JDLL_PATH`** — fallback only; never overrides a script-local `.jdll`

```bash
export JELLO_JDLL_PATH="/path/to/jello-compiler/build/lib"
./build/bin/jellovm path/to/module.jlo
```

Reference library: `jellovm/libs/std/` builds `std.jdll` (Neko `std.ndll` subset). **Authoring guide:** [`CFFI.md`](CFFI.md).

Optional profiling timestamps (monotonic clock where available):

```bash
JELLO_PROFILE=1 ./build/bin/jellovm path/to/module.jlo
```

### Diagnostic matrix (performance tuning)

Run from a Release build (`-DCMAKE_BUILD_TYPE=Release`, computed goto ON on GCC/Clang).

| Goal | Command | What to look for on stderr |
|------|---------|----------------------------|
| Phase timings + GC reuse | `JELLO_PROFILE=1 jellovm module.jlo` | `read_file`/`bc_read`/`vm_create`/`exec` ms; `freelist_hits` vs `freelist_misses`; top opcodes (`obj_new`, `obj_set_atom`, …) |
| JIT compile / reject | `JELLO_JIT_DUMP=1 jellovm module.jlo` | `compiled` vs `reject` for `boot`, `pair`, `advance` |
| Interpreter-only baseline | `jellovm --no-jit module.jlo` | Compare exec ms with JIT on |
| Disable JIT globally | `JELLO_JIT=0` or `--no-jit` | Same as above |

Recommended per-benchmark sweep (`module`, `nbodies`, `fib`, `fp`):

```bash
for b in module nbodies fib fp; do
  echo "=== $b ==="
  JELLO_PROFILE=1 ./build/bin/jellovm bench/out/$b.jlo 2>&1 | rg 'JELLO_PROFILE|freelist'
  JELLO_JIT_DUMP=1 ./build/bin/jellovm bench/out/$b.jlo 2>&1 | rg 'jit|compiled|reject'
  ./build/bin/jellovm --no-jit bench/out/$b.jlo
done
```

Windows x64: use `release.ps1` output; expect high `freelist_hits` on `module` and JIT `compiled` for `boot` when healthy.

### Local stress testing

For compiler + VM correctness under load (JIT vs interpreter diff, spill/branch/GC benches, optional Linux `perf`), run the local stress pipeline documented in [STRESS_TEST.md](../../STRESS_TEST.md):

```bash
cd jello-compiler
./bench/stress.sh quick    # ~5 min smoke
./bench/stress.sh full     # full bench + report
```

Windows: `.\bench\stress.ps1 quick`

### Safety limits (optional)

By default, execution is **uncapped** (like PUC-Rio Lua): no instruction budget. Optional host limits:

| Variable | Default | Effect |
|----------|---------|--------|
| `JELLO_FUEL` | off (`0`) | When set to `N > 0`, trap after `N` loop backedges + calls |
| `JELLO_MAX_BYTES` | 64 MiB | Max `Bytes` allocation |
| `JELLO_MAX_ARRAY` | 8 M | Max `Array` length |

## Partial JIT

The VM includes a tier-1 **partial function JIT** (ARM64 first) when built with `JELLOVM_ENABLE_JIT=ON` (default). Whitelisted opcodes (i32/i64/f32/f64 arithmetic, compare, negation, const pool, branches) compile to native code; heap and other opcodes use runtime fallback to `op_dispatch`. Hot loops **OSR** after enough backedges. **Numeric self-recursion** (`CALL` or proven `CONST_FUN`+`CALLR`) compiles with a specialized self-call/return path. Cross-function `CALL`/`TAILCALL` and dense slow ops stay in the interpreter. `TRY` rejects compilation. JIT is **on by default**; use `--no-jit` or the controls below to disable.

| Control | Effect |
|---------|--------|
| `--no-jit` | Disable JIT for this process (CLI) |
| `JELLO_JIT=0` | Disable JIT via environment |
| `JELLO_JIT_HOT=N` | Compile after N loop backedges, then OSR (default 32) |
| `JELLO_JIT_RUN=0` | Compile only; do not execute native code |
| `JELLO_JIT_DUMP=1` | Log compile / skip / emit-fail / OSR miss to stderr |
| `JELLO_JIT_BENCH=0` | ctest: correctness only (no timing line) |
| `JELLO_JIT_BENCH_GATE=1` | ctest: require ≥3× JIT speedup on AArch64 or x86-64 Release |
| `jello_vm_set_jit_enabled(vm, 0)` | Disable at runtime (embed API) |

### CMake options (selection)

| Option | Default | Notes |
|--------|---------|--------|
| `JELLOVM_ENABLE_JIT` | `ON` | Partial JIT; ARM64 on Apple Silicon, x86-64 on Intel (Phase 7) |
| `JELLOVM_BUILD_TESTS` | `ON` | When `ON`, expects a sibling **`jello-compiler`** checkout next to this repo (`../jello-compiler/ctest`). Set `OFF` to build the VM only. |
| `JELLOVM_USE_COMPUTED_GOTO` | `ON` in Release | Faster opcode dispatch on GCC/Clang; ignored on other compilers. |
| `JELLOVM_ENABLE_LTO` | `ON` in Release | Link-time optimization when supported. |
| `JELLOVM_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |
| `JELLOVM_ENABLE_ASAN` / `JELLOVM_ENABLE_UBSAN` | `OFF` | Sanitizers (GCC/Clang). |

Example, VM only (no test tree):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJELLOVM_BUILD_TESTS=OFF
cmake --build build
```

`compile_commands.json` is generated in the build directory when using CMake’s default behaviour (useful for clangd and other tools).

## Install

Install the VM runtime, embed headers, and `libjellovm` to your user profile:

```bash
./install.sh
```

Layout:

| Path | Purpose |
|------|---------|
| `~/.jello/vm/sdk/<version>/` | `bin/jellovm`, `lib/libjellovm.*`, `include/` |
| `~/.jello/vm/current` | Symlink to the active version |
| `~/.config/jello/vm-env.sh` | `JELLO_VM_ROOT`, `JELLO_INCLUDE`, `JELLO_LIB`, PATH |

On Windows, run `.\install.ps1` from the repo root (installs under `%LOCALAPPDATA%\Jello\vm\`).

Useful variants:

```bash
./install.sh --package-only        # build and create dist/jellovm-VERSION-OS-ARCH.zip
./install.sh --skip-build          # install from an existing build/
./install.sh --uninstall           # remove installed VM
```

Stage or package without installing:

```bash
scripts/stage-vm.sh --build-dir build --prefix /tmp/jellovm-stage
scripts/package-vm.sh --version 0.1.0 --os darwin --arch arm64 --build-dir build --out-dir dist
```

`cmake --install` is the source of truth for public headers (`jello.h`, `jello/jdll.h`) and libraries — the same staging used when the [Jello SDK](https://github.com/Lazarus404/jello-compiler) zip is built.

### Windows x86-64 performance (JIT)

Release benchmarks on Windows expect **MinGW-w64 Clang (or GCC) + Ninja + Release**. The x64 JIT backend emits **SysV AMD64** calls to C runtime helpers; **MSVC is not a JIT perf target** (`cmake` fails fast if `JELLOVM_ENABLE_JIT=ON` with MSVC).

From `jello-compiler/`:

```powershell
.\release.ps1
$env:JELLO_JIT_DUMP = "1"
.\build\bin\jellovm.exe bench\out\nbodies.jlo   # after benchmarks.ps1 compile step
.\benchmarks.ps1 --filter 'fib|nbodies|module|fp'
```

| Check | Command / signal |
|-------|------------------|
| JIT compiles hot funcs | `JELLO_JIT_DUMP=1` — look for `compiled` vs `reject` |
| Computed goto active | CMake status: `JELLOVM_USE_COMPUTED_GOTO: ON` (GCC/Clang only) |
| x64 JIT backend | `JELLOVM_JIT_X64: ON` in configure log |
| Object alloc reuse | `JELLO_PROFILE=1` — stderr `gc_freelist hits/misses` on `module` |

Use a **static** `jellovm` exe (default in `release.ps1`) so LTO applies; shared `jellovm.dll` builds disable LTO.

## Layout

| Path | Role |
|------|------|
| `src/include/jello.h` | Public C API |
| `src/include/jello/internal/` | VM-internal headers (`jit_internal.h` = VM hooks, `jit_impl.h` = JIT subsystem) |
| `src/vm/`, `src/bytecode/`, `src/types/`, `src/gc.c` | Interpreter, loader, and runtime |
| `src/jit/` | Optional partial JIT (IR, cache, ARM64 backend) |
| `cmake/` | Shared CMake modules (warnings, sanitizers) |

**Contributor docs:** [Part 12 — VM contributing](../jello-compiler/docs/08-virtual-machine/12-contributing/01-README.md). [Part 8 — Virtual machine](../jello-compiler/docs/08-virtual-machine/01-overview.md) — internals depth.

## License

See the BSD-style license headers in the source files (copyright 2019 - Jahred Love).
