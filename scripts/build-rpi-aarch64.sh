#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="aarch64-unknown-linux-gnu"
STAGE_DIR="$ROOT_DIR/output/rpi-aarch64"
RELEASE_DIR="$ROOT_DIR/target/$TARGET/release"

require_cmd() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "Missing required command: $name" >&2
    exit 1
  fi
}

require_cmd rustup
require_cmd cargo
require_cmd zig
require_cmd cargo-zigbuild

if ! cargo-zigbuild --version >/dev/null 2>&1; then
  echo "cargo-zigbuild is not installed. Run: cargo install cargo-zigbuild" >&2
  exit 1
fi

rustup target add "$TARGET" >/dev/null

cd "$ROOT_DIR"
cargo zigbuild --release --target "$TARGET" -p linkembed --bin linkembed

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/target/release" "$STAGE_DIR/scripts"

cp "$ROOT_DIR/ecosystem.config.js" "$STAGE_DIR/ecosystem.config.js"
cp "$ROOT_DIR/scripts/pm2_start.sh" "$STAGE_DIR/scripts/pm2_start.sh"
cp "$ROOT_DIR/scripts/pm2_restart.sh" "$STAGE_DIR/scripts/pm2_restart.sh"
cp "$ROOT_DIR/scripts/remote-rpi-postdeploy.sh" "$STAGE_DIR/scripts/remote-rpi-postdeploy.sh"
cp "$RELEASE_DIR/linkembed" "$STAGE_DIR/target/release/linkembed"
chmod +x "$STAGE_DIR"/scripts/*.sh "$STAGE_DIR/target/release/linkembed"

echo "Staged Raspberry Pi deployment bundle at $STAGE_DIR"
