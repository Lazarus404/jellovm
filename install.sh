#!/usr/bin/env bash
# Build jellovm, optionally package a VM zip, and install for the current user.
#
# Usage:
#   ./install.sh
#   ./install.sh --skip-build
#   ./install.sh --package-only
#   ./install.sh --uninstall
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

SKIP_BUILD=0
PACKAGE_ONLY=0
BUILD_DIR="build"
OUT_DIR="dist"
VERSION=""
PREFIX=""
DRY_RUN=0
UNINSTALL=0

usage() {
  cat <<'EOF'
usage: install.sh [options]

Build jellovm, optionally package a platform VM zip, and install locally.

Build / package:
  --skip-build       Skip cmake build (use existing build/)
  --package-only     Build and create dist/*.zip only; do not install
  --version VER      VM version label (default: CMake project version)
  --build-dir DIR    CMake build directory (default: build)
  --out-dir DIR      Output directory for VM zip (default: dist)

Install (passed to scripts/install-vm.sh):
  --prefix PATH      Override install root (~/.jello/vm)
  --dry-run          Print install actions without changing the system
  --uninstall        Remove an installed VM (pass --version if needed)
  -h, --help         Show this help
EOF
}

log() { printf '%s\n' "$*"; }

detect_version_from_cmake() {
  if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=\(.*\)/\1/p' "${BUILD_DIR}/CMakeCache.txt" | head -n 1
    return 0
  fi
  grep -E '^[[:space:]]*VERSION[[:space:]]+' CMakeLists.txt | head -n 1 | sed -E 's/.*VERSION[[:space:]]+([0-9.]+).*/\1/'
}

detect_os() {
  case "$(uname -s)" in
    Darwin) echo "darwin" ;;
    Linux) echo "linux" ;;
    MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
    *) echo "unknown" ;;
  esac
}

detect_arch() {
  local raw
  raw="$(uname -m)"
  case "$raw" in
    x86_64|amd64) echo "x64" ;;
    arm64|aarch64) echo "arm64" ;;
    *) echo "$raw" ;;
  esac
}

install_args=()
append_install_arg() { install_args+=("$@"); }

run_install() {
  exec "${ROOT}/scripts/install-vm.sh" "$@" "${install_args[@]}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) SKIP_BUILD=1; shift ;;
    --package-only) PACKAGE_ONLY=1; shift ;;
    --version) VERSION="$2"; append_install_arg --version "$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    --prefix) PREFIX="$2"; append_install_arg --prefix "$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; append_install_arg --dry-run; shift ;;
    --uninstall) UNINSTALL=1; append_install_arg --uninstall; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ "$UNINSTALL" -eq 1 ]]; then
  run_install
fi

OS_NAME="$(detect_os)"
ARCH="$(detect_arch)"
if [[ "$OS_NAME" == "windows" ]]; then
  echo "error: use install.ps1 on Windows" >&2
  exit 1
fi

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  log "Building jellovm (Release)..."
  if [[ "$DRY_RUN" -eq 1 ]]; then
    log "[dry-run] cmake -S . -B ${BUILD_DIR} -DCMAKE_BUILD_TYPE=Release -DJELLOVM_BUILD_TESTS=OFF"
    log "[dry-run] cmake --build ${BUILD_DIR} --target jellovm_cli"
  else
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DJELLOVM_BUILD_TESTS=OFF
    cmake --build "$BUILD_DIR" --target jellovm_cli
  fi
else
  log "Skipping build (--skip-build)."
fi

[[ -n "$VERSION" ]] || VERSION="$(detect_version_from_cmake)"
[[ -n "$VERSION" ]] || VERSION="0.1.0"

VM_ZIP="${OUT_DIR}/jellovm-${VERSION}-${OS_NAME}-${ARCH}.zip"

if [[ "$PACKAGE_ONLY" -eq 1 ]]; then
  log "Packaging VM ${VERSION} (${OS_NAME}-${ARCH})..."
  if [[ "$DRY_RUN" -eq 1 ]]; then
    log "[dry-run] scripts/package-vm.sh --version ${VERSION} --os ${OS_NAME} --arch ${ARCH} --build-dir ${BUILD_DIR} --out-dir ${OUT_DIR}"
  else
    ./scripts/package-vm.sh \
      --version "$VERSION" \
      --os "$OS_NAME" \
      --arch "$ARCH" \
      --build-dir "$BUILD_DIR" \
      --out-dir "$OUT_DIR"
  fi
  log "Created ${VM_ZIP}"
  exit 0
fi

log "Installing from build tree..."
if [[ "$DRY_RUN" -eq 1 ]]; then
  log "[dry-run] scripts/install-vm.sh --build-dir ${BUILD_DIR}"
  exit 0
fi

exec "${ROOT}/scripts/install-vm.sh" --build-dir "$BUILD_DIR" "${install_args[@]}"
