# LinkEmbed

Rust Discord bot that watches messages for URLs, waits briefly for Discord's native embed, and only posts its own fallback preview when Discord does not.

## Goals

- Follow Discord's native link card layout as closely as Discord custom embeds allow.
- Avoid duplicate previews by waiting before sending and deleting the bot preview if Discord later attaches its own embed.
- Work on as many sites as practical with plain HTTP metadata extraction.
- Keep deployment small and simple with a single Rust binary.

## Current Behavior

- Detects `http://` and `https://` URLs in new messages.
- Waits `embed_delay_seconds` before sending a fallback embed.
- Re-checks the original message before sending.
- Deletes the bot reply if the original message is edited to remove the URL.
- Deletes the bot reply if Discord later creates a native embed.
- Extracts metadata from `<title>`, Open Graph tags, Twitter tags, and standard `description`.
- Resolves relative image URLs against the final page URL.
- Supports an optional public image proxy for hosts that block direct image fetches.
- Caches metadata in memory with TTL, LRU eviction, entry count limit, and byte budget limit.

## Project Layout

- `Cargo.toml` workspace root and bot package manifest
- `crates/linkembed-core/` for preview fetching, parsing, caching, and URL utilities
- `src/app/` for Discord event handling, job scheduling, reply tracking, and embed rendering
- `src/config.rs`
- `src/logger.rs`

## Requirements

- Rust toolchain via `rustup`
- Cargo

Install Rust with [rustup](https://rustup.rs/).

Do not rely on an old distro-packaged `cargo`/`rustc`. This project tracks the stable Rust toolchain via `rust-toolchain.toml`.

## Build

```bash
cargo build --release
```

or:

```bash
./scripts/build_release.sh
```

## Run

```bash
cargo run --release
```

The workspace keeps `linkembed` as the default member, so these commands still build and run the same bot binary from the repository root.

On first launch, the bot creates `config/config.json` next to the compiled binary and exits.

For a release build run from the repository root, that file will be created at:

```text
target/release/config/config.json
```

Set `bot_token` and start the bot again.

## Configuration

```json
{
  "embed_delay_seconds": 5,
  "cache_ttl_minutes": 10,
  "cache_max_size": 1000,
  "cache_max_bytes": 33554432,
  "http_timeout_ms": 4000,
  "http_max_redirects": 5,
  "http_user_agent": "LinkEmbedBot/1.0 (+https://github.com/Ayumudayo/LinkEmbed; Discord link preview bot)",
  "max_concurrency": 0,
  "rate_per_sec": 2.0,
  "max_html_bytes": 8388608,
  "html_initial_range_bytes": 524288,
  "html_range_growth_factor": 2.0,
  "bot_token": "YOUR_BOT_TOKEN_HERE",
  "log_level": "info",
  "image_proxy_enabled": true,
  "image_proxy_base": "https://images.weserv.nl",
  "image_proxy_query": "w=1200&h=630&fit=inside",
  "image_proxy_hosts": ["dcinside.co.kr"]
}
```

Notes:

- `max_concurrency = 0` means half of the available CPU cores.
- `http_user_agent` should stay explicit and stable.
- `image_proxy_*` is only used for image hosts that need a proxy workaround.

## PM2

`ecosystem.config.js` points to the Rust release binary:

```bash
./scripts/pm2_start.sh
./scripts/pm2_restart.sh
```

These scripts do not build the project. Build first, then start or restart PM2.

## Raspberry Pi / PM2

For a Raspberry Pi or Ubuntu-style server where you want to keep the bot under `pm2`, use the release binary and PM2 helpers.

1. Build the release binary:

```bash
./scripts/build_release.sh
```

2. Run it once to generate the default config:

```bash
./target/release/linkembed
```

3. Edit `target/release/config/config.json` and set `bot_token`.

4. Start the long-running bot with `pm2`:

```bash
./scripts/pm2_start.sh
```

Useful commands:

```bash
pm2 status LinkEmbed-Bot
pm2 logs LinkEmbed-Bot
./scripts/pm2_restart.sh
pm2 delete LinkEmbed-Bot
```

### Cross-build On Your PC And Deploy To Remote server (eg. Ubuntu Server)

If you do not want to compile on the Raspberry Pi itself, build an `aarch64-unknown-linux-gnu` binary on this machine and push it over SSH.

Prerequisites on this machine:

- `zig` on `PATH`
- `cargo-zigbuild` installed:
  - `cargo install cargo-zigbuild`
- SSH access to the Raspberry Pi

Build a deployment bundle:

```powershell
./scripts/build-rpi-aarch64.ps1
```

```bash
./scripts/build-rpi-aarch64.sh
```

Deploy and restart remotely:

```powershell
./scripts/deploy-rpi-aarch64.ps1 -RemoteHost <pi-host> -RemoteUser <pi-user>
```

```bash
./scripts/deploy-rpi-aarch64.sh --host <pi-host> --user <pi-user>
```

Optional flags:

- `--skip-build` / `-SkipBuild`
- `--port` / `-Port`
- `--app-dir` / `-AppDir`
- `--key` / `-KeyPath`

The deploy script stages a compact bundle under `output/rpi-aarch64/`, uploads it as a single archive, extracts it remotely, and starts or restarts `pm2`.
If `target/release/config/config.json` does not exist on the target yet, the post-deploy step runs the binary once to generate the default config and stops so you can fill `bot_token`.
If you configure SSH key authentication and pass `--key` / `-KeyPath` (or set `RPI_SSH_KEY`), repeated password prompts are eliminated.

## License

This project is licensed under `LICENSE.txt`.
