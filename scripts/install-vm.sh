#!/usr/bin/env bash
# Install a Jello VM component (release zip, staged prefix, or build directory).
#
# Usage:
#   ./install-vm.sh --zip jellovm-0.1.0-darwin-arm64.zip
#   ./install-vm.sh --dir ./jellovm-0.1.0-darwin-arm64
#   ./install-vm.sh --build-dir build
#   ./install-vm.sh --uninstall
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

ZIP=""
DIR=""
BUILD_DIR=""
VERSION=""
PREFIX=""
UNINSTALL=0
DRY_RUN=0

usage() {
  cat <<'EOF'
usage: install-vm.sh [options]

Install the Jello VM (jellovm, libjellovm, embed/JDLL headers).

Options:
  --zip PATH         Install from a VM release zip
  --dir PATH         Install from an unpacked VM directory
  --build-dir DIR    Stage via cmake --install from a build tree, then install
  --version VER      VM version (auto-detected from zip/dir name when omitted)
  --prefix PATH      Override install root (default: ~/.jello/vm)
  --uninstall        Remove an installed VM version
  --dry-run          Print actions without changing the system
  -h, --help         Show this help

Layout (default):
  ~/.jello/vm/sdk/<version>/   VM files (bin, lib, include)
  ~/.jello/vm/current          Symlink to active version
  ~/.config/jello/vm-env.sh    Environment exports

After install, open a new terminal or run:
  source ~/.config/jello/vm-env.sh
EOF
}

log() { printf '%s\n' "$*"; }
run() {
  if [[ "$DRY_RUN" -eq 1 ]]; then
    log "[dry-run] $*"
  else
    "$@"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --zip) ZIP="$2"; shift 2 ;;
    --dir) DIR="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --prefix) PREFIX="$2"; shift 2 ;;
    --uninstall) UNINSTALL=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage >&2; exit 1 ;;
  esac
done

validate_vm_root() {
  local root="$1"
  if [[ ! -d "$root/bin" || ! -d "$root/lib" || ! -d "$root/include" ]]; then
    echo "error: '$root' is not a Jello VM tree (expected bin/, lib/, include/)" >&2
    exit 1
  fi
  if [[ ! -f "$root/include/jello.h" || ! -f "$root/include/jello/jdll.h" ]]; then
    echo "error: VM headers not found under '$root/include'" >&2
    exit 1
  fi
  if [[ ! -f "$root/bin/jellovm" && ! -f "$root/bin/jellovm.exe" ]]; then
    echo "error: jellovm not found under '$root/bin'" >&2
    exit 1
  fi
}

detect_version_from_name() {
  local name="$1"
  if [[ "$name" =~ ^jellovm-([0-9]+\.[0-9]+\.[0-9]+(-[^-]+)?)- ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  return 1
}

detect_version() {
  local root="$1"
  if [[ -n "$VERSION" ]]; then
    echo "$VERSION"
    return 0
  fi
  local from_name
  from_name="$(detect_version_from_name "$(basename "$root")" || true)"
  if [[ -n "$from_name" ]]; then
    echo "$from_name"
    return 0
  fi
  if [[ -f "$root/README.txt" ]]; then
    local line
    line="$(sed -n '1p' "$root/README.txt")"
    if [[ "$line" =~ Jello\ VM\ ([0-9]+\.[0-9]+\.[0-9]+(-[^[:space:]]+)?) ]]; then
      echo "${BASH_REMATCH[1]}"
      return 0
    fi
  fi
  echo "error: could not detect VM version; pass --version" >&2
  exit 1
}

resolve_vm_root() {
  local tmp=""
  if [[ -n "$ZIP" && -n "$DIR" ]]; then
    echo "error: use only one of --zip or --dir" >&2
    exit 1
  fi
  if [[ -n "$BUILD_DIR" && ( -n "$ZIP" || -n "$DIR" ) ]]; then
    echo "error: --build-dir cannot be combined with --zip or --dir" >&2
    exit 1
  fi

  if [[ -n "$BUILD_DIR" ]]; then
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/jellovm-stage.XXXXXX")"
    if [[ "$DRY_RUN" -eq 1 ]]; then
      log "[dry-run] stage-vm.sh --build-dir ${BUILD_DIR} --prefix ${tmp}"
      echo "${tmp}/dry-run"
      return 0
    fi
    "${ROOT}/scripts/stage-vm.sh" --build-dir "$BUILD_DIR" --prefix "$tmp"
    validate_vm_root "$tmp"
    echo "$tmp"
    return 0
  fi

  if [[ -n "$ZIP" ]]; then
    [[ -f "$ZIP" ]] || { echo "error: zip not found: $ZIP" >&2; exit 1; }
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/jellovm-install.XXXXXX")"
    if [[ "$DRY_RUN" -eq 1 ]]; then
      log "[dry-run] unzip '$ZIP' -> temp dir"
      echo "${tmp}/dry-run"
      return 0
    fi
    unzip -q "$ZIP" -d "$tmp"
    local extracted
    extracted="$(find "$tmp" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
    [[ -n "$extracted" ]] || { echo "error: empty zip: $ZIP" >&2; exit 1; }
    validate_vm_root "$extracted"
    echo "$extracted"
    return 0
  fi

  if [[ -n "$DIR" ]]; then
    DIR="$(cd "$DIR" && pwd)"
    validate_vm_root "$DIR"
    echo "$DIR"
    return 0
  fi

  if [[ -f "$SCRIPT_DIR/bin/jellovm" || -f "$SCRIPT_DIR/bin/jellovm.exe" ]]; then
    validate_vm_root "$SCRIPT_DIR"
    echo "$SCRIPT_DIR"
    return 0
  fi

  echo "error: pass --zip, --dir, --build-dir, or run from an unpacked VM tree" >&2
  usage >&2
  exit 1
}

default_vm_home() {
  if [[ -n "$PREFIX" ]]; then
    echo "$PREFIX"
    return 0
  fi
  echo "${HOME}/.jello/vm"
}

env_file_path() {
  echo "${HOME}/.config/jello/vm-env.sh"
}

write_env_file() {
  local vm_home="$1"
  local env_file
  env_file="$(env_file_path)"
  local content
  content="# Jello VM — generated by install-vm.sh
# shellcheck disable=SC2034
export JELLO_VM_ROOT=\"${vm_home}/current\"
export PATH=\"\${JELLO_VM_ROOT}/bin:\${PATH}\"
export JELLO_INCLUDE=\"\${JELLO_VM_ROOT}/include\"
export JELLO_LIB=\"\${JELLO_VM_ROOT}/lib\"
export CPATH=\"\${JELLO_INCLUDE}\${CPATH:+:\${CPATH}}\"
export C_INCLUDE_PATH=\"\${JELLO_INCLUDE}\${C_INCLUDE_PATH:+:\${C_INCLUDE_PATH}}\"
export LIBRARY_PATH=\"\${JELLO_LIB}\${LIBRARY_PATH:+:\${LIBRARY_PATH}}\"
"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    log "[dry-run] write $env_file"
    return 0
  fi
  run mkdir -p "$(dirname "$env_file")"
  printf '%s' "$content" >"$env_file"
  run chmod 644 "$env_file"
}

profile_marker_begin="# >>> jello-vm >>>"
profile_marker_end="# <<< jello-vm <<<"

hook_user_profile() {
  local env_file
  env_file="$(env_file_path)"
  local hook="[ -f \"${env_file}\" ] && . \"${env_file}\""
  local profiles=()
  if [[ -f "${HOME}/.zprofile" ]]; then profiles+=("${HOME}/.zprofile"); fi
  if [[ -f "${HOME}/.zshrc" ]]; then profiles+=("${HOME}/.zshrc"); fi
  if [[ -f "${HOME}/.bash_profile" ]]; then profiles+=("${HOME}/.bash_profile"); fi
  if [[ -f "${HOME}/.bashrc" ]]; then profiles+=("${HOME}/.bashrc"); fi
  if [[ -f "${HOME}/.profile" ]]; then profiles+=("${HOME}/.profile"); fi

  local profile
  for profile in "${profiles[@]}"; do
    if grep -Fq "$profile_marker_begin" "$profile" 2>/dev/null; then
      continue
    fi
    if [[ "$DRY_RUN" -eq 1 ]]; then
      log "[dry-run] append jello-vm hook to $profile"
      continue
    fi
    {
      echo ""
      echo "$profile_marker_begin"
      echo "$hook"
      echo "$profile_marker_end"
    } >>"$profile"
    log "Updated shell profile: $profile"
  done
}

install_vm() {
  local vm_root="$1"
  local version="$2"
  local vm_home
  vm_home="$(default_vm_home)"
  local target="${vm_home}/sdk/${version}"
  local current="${vm_home}/current"

  log "Installing Jello VM ${version} -> ${target}"

  run mkdir -p "${vm_home}/sdk"
  if [[ -d "$target" ]]; then
    echo "error: ${target} already exists (use --uninstall first)" >&2
    exit 1
  fi
  run cp -R "$vm_root/." "$target"
  run ln -sfn "$target" "$current"

  write_env_file "$vm_home"
  hook_user_profile

  log ""
  log "Installed Jello VM ${version}."
  log "  JELLO_VM_ROOT=${current}"
  log "Open a new terminal, or run: source $(env_file_path)"
  log ""
  log "Verify:"
  log "  jellovm --help"
}

uninstall_vm() {
  local vm_home
  vm_home="$(default_vm_home)"
  local version="${VERSION:-}"
  local target=""

  if [[ -z "$version" ]]; then
    if [[ -L "${vm_home}/current" ]]; then
      target="$(readlink "${vm_home}/current")"
      version="$(basename "$target")"
    else
      echo "error: pass --version or ensure ${vm_home}/current exists" >&2
      exit 1
    fi
  fi

  target="${vm_home}/sdk/${version}"
  log "Removing ${target}"
  run rm -rf "$target"
  if [[ -L "${vm_home}/current" ]] && [[ "$(readlink "${vm_home}/current")" == "$target" ]]; then
    run rm -f "${vm_home}/current"
    local newest=""
    newest="$(ls -1d "${vm_home}/sdk/"* 2>/dev/null | sort -V | tail -n 1 || true)"
    if [[ -n "$newest" ]]; then
      run ln -sfn "$newest" "${vm_home}/current"
      log "Switched current -> $(basename "$newest")"
    fi
  fi
  log "Uninstalled Jello VM ${version}."
}

if [[ "$UNINSTALL" -eq 1 ]]; then
  uninstall_vm
  exit 0
fi

VM_ROOT="$(resolve_vm_root)"
if [[ "$DRY_RUN" -eq 0 ]]; then
  validate_vm_root "$VM_ROOT"
fi
VERSION="$(detect_version "$VM_ROOT")"
install_vm "$VM_ROOT" "$VERSION"
