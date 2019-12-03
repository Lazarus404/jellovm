#!/usr/bin/env bash
# Stage a Jello VM install tree via `cmake --install` (bin/, lib/, include/).
# Usage:
#   scripts/stage-vm.sh --build-dir build --prefix /tmp/jellovm-stage
set -euo pipefail

BUILD_DIR=""
PREFIX=""

usage() {
  echo "usage: $0 --build-dir DIR --prefix DIR" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --prefix) PREFIX="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "unknown arg: $1" >&2; usage ;;
  esac
done

[[ -n "$BUILD_DIR" && -n "$PREFIX" ]] || usage

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

resolve_build_dir() {
  local dir="$1"
  if [[ "$dir" = /* ]]; then
    if [[ -f "${dir}/CMakeCache.txt" ]]; then
      cd "$dir" && pwd
      return 0
    fi
    echo "error: CMake build not found at ${dir} (missing CMakeCache.txt)" >&2
    exit 1
  fi
  if [[ -f "${ROOT}/${dir}/CMakeCache.txt" ]]; then
    cd "${ROOT}/${dir}" && pwd
    return 0
  fi
  if [[ -f "${dir}/CMakeCache.txt" ]]; then
    cd "$dir" && pwd
    return 0
  fi
  echo "error: CMake build not found at ${dir} (missing CMakeCache.txt)" >&2
  exit 1
}

BUILD_DIR="$(resolve_build_dir "$BUILD_DIR")"
mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd)"

cmake --install "$BUILD_DIR" --prefix "$PREFIX"

if [[ ! -f "${PREFIX}/include/jello.h" ]]; then
  echo "error: jello.h not found under ${PREFIX}/include after cmake --install" >&2
  exit 1
fi
if [[ ! -f "${PREFIX}/include/jello/jdll.h" ]]; then
  echo "error: jello/jdll.h not found under ${PREFIX}/include after cmake --install" >&2
  exit 1
fi
if [[ ! -f "${PREFIX}/bin/jellovm" && ! -f "${PREFIX}/bin/jellovm.exe" ]]; then
  echo "error: jellovm not found under ${PREFIX}/bin after cmake --install" >&2
  exit 1
fi

has_lib=0
for candidate in \
  "${PREFIX}/lib/libjellovm.a" \
  "${PREFIX}/lib/libjellovm.lib" \
  "${PREFIX}/lib/libjellovm.dll.a"; do
  if [[ -f "$candidate" ]]; then
    has_lib=1
    break
  fi
done
if [[ "$has_lib" -eq 0 ]]; then
  echo "error: libjellovm not found under ${PREFIX}/lib after cmake --install" >&2
  exit 1
fi

# Windows shared builds may place runtime DLLs under lib/; SDK layout expects them in bin/.
shopt -s nullglob
for dll in "${PREFIX}/lib/"*jellovm*.dll; do
  mkdir -p "${PREFIX}/bin"
  cp -f "$dll" "${PREFIX}/bin/"
done

echo "Staged VM install at ${PREFIX}"
