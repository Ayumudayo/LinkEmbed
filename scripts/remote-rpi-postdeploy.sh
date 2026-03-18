#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$1"
ARCHIVE_PATH="$2"
CONFIG_PATH="$APP_DIR/target/release/config/config.json"
BINARY_PATH="$APP_DIR/target/release/linkembed"

for profile in "$HOME/.profile" "$HOME/.bash_profile" "$HOME/.bashrc"; do
  if [[ -f "$profile" ]]; then
    # shellcheck disable=SC1090
    source "$profile"
  fi
done

if [[ -d "$HOME/.nvm/versions/node" ]]; then
  latest_node_bin="$(find "$HOME/.nvm/versions/node" -path '*/bin/node' -printf '%h\n' 2>/dev/null | sort | tail -n 1 || true)"
  if [[ -n "$latest_node_bin" ]]; then
    export PATH="$latest_node_bin:$PATH"
  fi
fi

if ! command -v pm2 >/dev/null 2>&1; then
  if [[ -d "$HOME/.nvm/versions/node" ]]; then
    latest_pm2_dir="$(find "$HOME/.nvm/versions/node" -path '*/bin/pm2' -printf '%h\n' 2>/dev/null | sort | tail -n 1 || true)"
    if [[ -n "$latest_pm2_dir" ]]; then
      export PATH="$latest_pm2_dir:$PATH"
    fi
  fi
fi

mkdir -p "$APP_DIR" "$APP_DIR/scripts" "$APP_DIR/target/release" "$APP_DIR/logs"
tar -C "$APP_DIR" -xf "$ARCHIVE_PATH"
rm -f "$ARCHIVE_PATH"

chmod +x "$APP_DIR"/scripts/*.sh "$BINARY_PATH"

cd "$APP_DIR"

if [[ ! -f "$CONFIG_PATH" ]]; then
  echo "Config missing at $CONFIG_PATH. Running the bot once to generate a default config..."
  "$BINARY_PATH"
fi

if [[ ! -f "$CONFIG_PATH" ]]; then
  echo "Config was not created at $CONFIG_PATH." >&2
  exit 1
fi

if grep -Eq '"bot_token"[[:space:]]*:[[:space:]]*"(YOUR_BOT_TOKEN_HERE)?"' "$CONFIG_PATH"; then
  echo "Config exists at $CONFIG_PATH but bot_token is still empty or default. Update it and rerun deployment." >&2
  exit 1
fi

if ! command -v pm2 >/dev/null 2>&1; then
  echo "PATH=$PATH" >&2
  echo "pm2 is not installed on the target host." >&2
  exit 1
fi

echo "Using pm2 from: $(command -v pm2)"
echo "Using node from: $(command -v node || echo missing)"
if ! ./scripts/pm2_restart.sh; then
  echo "PM2 restart failed. Dumping pm2 status and recent logs..." >&2
  pm2 status || true
  pm2 logs LinkEmbed-Bot --lines 80 --nostream || true
  exit 1
fi
