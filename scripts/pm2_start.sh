#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

BINARY="$PROJECT_ROOT/target/release/linkembed"
CONFIG="$PROJECT_ROOT/target/release/config/config.json"

if [[ ! -x "$BINARY" ]]; then
  echo "Missing release binary: $BINARY"
  echo "Run ./scripts/build_release.sh first."
  exit 1
fi

if [[ ! -f "$CONFIG" ]]; then
  echo "Missing config file: $CONFIG"
  echo "Run ./target/release/linkembed once to generate it, then edit bot_token."
  exit 1
fi

if pm2 describe LinkEmbed-Bot >/dev/null 2>&1; then
  echo "PM2 app already exists. Use ./scripts/pm2_restart.sh instead."
  exit 1
fi

pm2 start ecosystem.config.js --update-env
pm2 save
pm2 status LinkEmbed-Bot
