#!/usr/bin/env bash
set -euo pipefail

echo "[check] Starting Linux environment preflight checks..."

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[miss] Command '$1' not found. Please install it via your package manager." >&2
    return 1
  else
    echo "[ ok ] $1: $(command -v "$1")"
  fi
}

fail=0

require_cmd cmake || fail=1
require_cmd ninja || fail=1
require_cmd pkg-config || fail=1

if (( fail )); then
  echo "[hint] Ubuntu/Debian example: sudo apt update && sudo apt install -y build-essential ninja-build pkg-config"
fi

# Check CMake version (>= 3.22 recommended)
if command -v cmake >/dev/null 2>&1; then
  ver=$(cmake --version | awk 'NR==1{print $3}')
  major=${ver%%.*}; rest=${ver#*.}; minor=${rest%%.*}
  if (( major < 3 || (major == 3 && minor < 22) )); then
    echo "[warn] Detected CMake ${ver}. Recommended version is >= 3.22."
    echo "[hint] Consider installing a newer CMake (e.g., sudo snap install cmake --classic)."
  else
    echo "[ ok ] CMake version: ${ver}"
  fi
fi

# Check VCPKG_ROOT and verify toolchain path
if [[ -z "${VCPKG_ROOT:-}" ]]; then
  echo "[miss] Environment variable VCPKG_ROOT is not set."
  echo "[hint] export VCPKG_ROOT=/path/to/vcpkg"
  fail=1
else
  echo "[ ok ] VCPKG_ROOT=${VCPKG_ROOT}"
  toolchain="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
  if [[ ! -f "$toolchain" ]]; then
    echo "[miss] vcpkg toolchain file not found: $toolchain"
    echo "[hint] Please verify the VCPKG_ROOT path."
    fail=1
  else
    echo "[ ok ] toolchain: $toolchain"
  fi
fi

# Check lexbor via pkg-config (warn if missing)
if command -v pkg-config >/dev/null 2>&1; then
  if pkg-config --exists lexbor; then
    echo "[ ok ] pkg-config: 'lexbor' is available"
    echo "      CFLAGS: $(pkg-config --cflags lexbor)"
    echo "      LIBS  : $(pkg-config --libs lexbor)"
  else
    echo "[warn] pkg-config could not find 'lexbor'."
    echo "[hint] Even if installed via vcpkg, a .pc file may be missing. CMake will fall back to alternative discovery paths."
  fi
fi

if (( fail )); then
  echo "[result] Some requirements are missing or warned. Please address the hints above and retry."
  exit 1
fi

echo "[done] Linux environment check passed. You can proceed to build."
