#!/usr/bin/env bash
# Package a platform VM zip from a CMake build tree (via cmake --install staging).
# Usage:
#   scripts/package-vm.sh --version 0.1.0 --os darwin --arch arm64 \
#     --build-dir build --out-dir dist
set -euo pipefail

VERSION=""
OS_NAME=""
ARCH=""
BUILD_DIR=""
OUT_DIR=""

usage() {
  echo "usage: $0 --version VER --os OS --arch ARCH --build-dir DIR --out-dir DIR" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="$2"; shift 2 ;;
    --os) OS_NAME="$2"; shift 2 ;;
    --arch) ARCH="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "unknown arg: $1" >&2; usage ;;
  esac
done

[[ -n "$VERSION" && -n "$OS_NAME" && -n "$ARCH" && -n "$BUILD_DIR" && -n "$OUT_DIR" ]] || usage

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$(cd "$ROOT" && cd "$BUILD_DIR" && pwd)"
OUT_DIR="$(cd "$ROOT" && mkdir -p "$OUT_DIR" && cd "$OUT_DIR" && pwd)"

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/jellovm-pkg.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

PKG_NAME="jellovm-${VERSION}-${OS_NAME}-${ARCH}"
PKG_ROOT="${STAGE}/${PKG_NAME}"

"${ROOT}/scripts/stage-vm.sh" --build-dir "$BUILD_DIR" --prefix "$PKG_ROOT"

cp "${ROOT}/scripts/install-vm.sh" "${PKG_ROOT}/install.sh"
chmod +x "${PKG_ROOT}/install.sh"
cp "${ROOT}/scripts/install-vm.ps1" "${PKG_ROOT}/install.ps1"

cat > "${PKG_ROOT}/README.txt" <<EOF
Jello VM ${VERSION} (${OS_NAME}-${ARCH})
=====================================

bin/jellovm   - Jello virtual machine
lib/          - libjellovm (static or import library)
include/      - jello.h (embed API), jello/jdll.h (plugin API)

Install (recommended):

  macOS / Linux:  ./install.sh
  Windows:        .\\install.ps1

This installs to ~/.jello/vm/, adds jellovm to PATH, and sets JELLO_INCLUDE and JELLO_LIB.

Manual environment:

  JELLO_INCLUDE=<vm>/include
  JELLO_LIB=<vm>/lib
EOF

ZIP_PATH="${OUT_DIR}/${PKG_NAME}.zip"
rm -f "$ZIP_PATH"

if command -v zip >/dev/null 2>&1; then
  (cd "$STAGE" && zip -rq "$ZIP_PATH" "$PKG_NAME")
else
  tar -a -cf "$ZIP_PATH" -C "$STAGE" "$PKG_NAME"
fi

echo "Created ${ZIP_PATH}"
