#!/usr/bin/env bash

set -euo pipefail

APPLY=0
PURGE_SHELL=0
REMOVE_PROJECT_VCPKG=0
QUIET=0

usage() {
  cat <<'EOF'
Usage:
  ./scripts/remove_vcpkg_linux.sh [--apply] [--purge-shell] [--remove-project-vcpkg] [--quiet]

What it does:
  - Detects likely vcpkg roots, caches, shell exports, and symlinks on Linux
  - Prints what would be removed by default
  - Removes files only when --apply is provided

Options:
  --apply                 Actually delete detected vcpkg roots/caches/symlinks
  --purge-shell           Remove VCPKG-related exports from shell startup files
  --remove-project-vcpkg  Also remove repo-local vcpkg directories if present
  --quiet                 Reduce informational output
  -h, --help              Show this help

Examples:
  ./scripts/remove_vcpkg_linux.sh
  ./scripts/remove_vcpkg_linux.sh --apply
  ./scripts/remove_vcpkg_linux.sh --apply --purge-shell
  ./scripts/remove_vcpkg_linux.sh --apply --remove-project-vcpkg
EOF
}

log() {
  if [[ "$QUIET" -eq 0 ]]; then
    printf '%s\n' "$*"
  fi
}

warn() {
  printf 'WARN: %s\n' "$*" >&2
}

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply)
      APPLY=1
      ;;
    --purge-shell)
      PURGE_SHELL=1
      ;;
    --remove-project-vcpkg)
      REMOVE_PROJECT_VCPKG=1
      ;;
    --quiet)
      QUIET=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown option: $1"
      ;;
  esac
  shift
done

if [[ "$PURGE_SHELL" -eq 1 && "$APPLY" -ne 1 ]]; then
  die "--purge-shell requires --apply"
fi

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFAULT_CACHE_BASE="${XDG_CACHE_HOME:-$HOME/.cache}"
DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-$DEFAULT_CACHE_BASE/vcpkg/archives}"

declare -a CANDIDATE_DIRS=()
declare -a SHELL_FILES=()
declare -a PROJECT_DIRS=()

append_unique() {
  local value="$1"
  [[ -z "$value" ]] && return 0
  local existing
  for existing in "${CANDIDATE_DIRS[@]:-}"; do
    [[ "$existing" == "$value" ]] && return 0
  done
  CANDIDATE_DIRS+=("$value")
}

append_existing_shell_file() {
  local value="$1"
  [[ -f "$value" ]] && SHELL_FILES+=("$value")
}

append_project_dir() {
  local value="$1"
  [[ -e "$value" ]] && PROJECT_DIRS+=("$value")
}

if [[ -n "${VCPKG_ROOT:-}" ]]; then
  append_unique "$VCPKG_ROOT"
fi

append_unique "$DEFAULT_BINARY_CACHE"
append_unique "$DEFAULT_CACHE_BASE/vcpkg"
append_unique "$HOME/vcpkg"
append_unique "/opt/vcpkg"
append_unique "/usr/local/vcpkg"

append_existing_shell_file "$HOME/.bashrc"
append_existing_shell_file "$HOME/.zshrc"
append_existing_shell_file "$HOME/.profile"
append_existing_shell_file "$HOME/.bash_profile"
append_existing_shell_file "$HOME/.zprofile"
append_existing_shell_file "/etc/environment"

append_project_dir "$PROJECT_ROOT/vcpkg_installed"
append_project_dir "$PROJECT_ROOT/.vcpkg_cache"
append_project_dir "$PROJECT_ROOT/.vcpkg_installed"
append_project_dir "$PROJECT_ROOT/vcpkg_cache"

log "vcpkg cleanup scan"
log "Project root: $PROJECT_ROOT"
log "VCPKG_ROOT: ${VCPKG_ROOT:-<unset>}"
log "VCPKG_DEFAULT_BINARY_CACHE: ${VCPKG_DEFAULT_BINARY_CACHE:-<unset>}"
log "VCPKG_BINARY_SOURCES: ${VCPKG_BINARY_SOURCES:-<unset>}"
log "X_VCPKG_ASSET_SOURCES: ${X_VCPKG_ASSET_SOURCES:-<unset>}"
log "Default cache base: $DEFAULT_CACHE_BASE"
log ""

log "Candidate directories:"
for dir in "${CANDIDATE_DIRS[@]}"; do
  if [[ -e "$dir" ]]; then
    log "  [found] $dir"
  else
    log "  [miss ] $dir"
  fi
done
log ""

log "Shell config references:"
FOUND_SHELL_REF=0
for file in "${SHELL_FILES[@]}"; do
  if grep -nE 'VCPKG_|(^|/|\s)vcpkg($|/|\s)' "$file" >/dev/null 2>&1; then
    FOUND_SHELL_REF=1
    log "  [found] $file"
    grep -nE 'VCPKG_|(^|/|\s)vcpkg($|/|\s)' "$file" || true
  fi
done
if [[ "$FOUND_SHELL_REF" -eq 0 ]]; then
  log "  none found"
fi
log ""

log "Executable / symlink checks:"
if command -v vcpkg >/dev/null 2>&1; then
  VCPKG_CMD="$(command -v vcpkg)"
  log "  vcpkg command: $VCPKG_CMD"
  if [[ -L "$VCPKG_CMD" ]]; then
    log "  symlink target: $(readlink -f "$VCPKG_CMD")"
  fi
else
  log "  vcpkg command: not found"
fi
log ""

if [[ "$REMOVE_PROJECT_VCPKG" -eq 1 ]]; then
  log "Project-local vcpkg directories marked for deletion:"
  for dir in "${PROJECT_DIRS[@]}"; do
    log "  $dir"
  done
  log ""
fi

if [[ "$APPLY" -ne 1 ]]; then
  log "Dry run only. Re-run with --apply to delete detected paths."
  exit 0
fi

log "Applying cleanup..."

for dir in "${CANDIDATE_DIRS[@]}"; do
  if [[ -e "$dir" ]]; then
    log "  removing $dir"
    rm -rf --one-file-system "$dir"
  fi
done

if [[ "$REMOVE_PROJECT_VCPKG" -eq 1 ]]; then
  for dir in "${PROJECT_DIRS[@]}"; do
    if [[ -e "$dir" ]]; then
      log "  removing project dir $dir"
      rm -rf --one-file-system "$dir"
    fi
  done
fi

if command -v vcpkg >/dev/null 2>&1; then
  VCPKG_CMD="$(command -v vcpkg)"
  if [[ -L "$VCPKG_CMD" ]]; then
    log "  removing symlink $VCPKG_CMD"
    rm -f "$VCPKG_CMD"
  else
    warn "vcpkg command exists but is not a symlink: $VCPKG_CMD"
  fi
fi

if [[ "$PURGE_SHELL" -eq 1 ]]; then
  for file in "${SHELL_FILES[@]}"; do
    if [[ -f "$file" ]]; then
      tmp="$(mktemp)"
      awk '
        /VCPKG_/ { next }
        /(^|\/|[[:space:]])vcpkg($|\/|[[:space:]])/ { next }
        { print }
      ' "$file" > "$tmp"
      if ! cmp -s "$file" "$tmp"; then
        log "  rewriting $file"
        cp "$file" "$file.bak"
        mv "$tmp" "$file"
      else
        rm -f "$tmp"
      fi
    fi
  done
fi

log ""
log "Cleanup complete."
log "Open a new shell or run: hash -r"
