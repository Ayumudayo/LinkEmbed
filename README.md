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

- `Cargo.toml`
- `src/app.rs`
- `src/config.rs`
- `src/fetch.rs`
- `src/metadata.rs`
- `src/cache.rs`

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

## CI

GitHub Actions now runs `cargo test` on Linux and Windows in `.github/workflows/pr_ci.yml`.

## Removing The Old C++ Bot On Linux

If you previously deployed the legacy C++ bot, remove it before switching production traffic to the Rust bot.

### 1. Stop the running process

If you used PM2:

```bash
pm2 delete LinkEmbed-Bot
```

If you started it manually:

```bash
pkill -f '/LinkEmbed$'
```

If you wrapped it in a service manager, stop that service first.

### 2. Remove the old C++ build outputs

From the repository root:

```bash
rm -rf build
rm -rf vcpkg_installed .vcpkg_cache vcpkg_cache
```

### 3. Remove old helper files you no longer need

```bash
rm -f ecosystem.config.js.old
rm -rf cmake config scripts tests
```

Only do this if you still have those legacy directories on the Linux host.

If you want a guided cleanup first, use:

```bash
./scripts/remove_vcpkg_linux.sh
./scripts/remove_vcpkg_linux.sh --apply
./scripts/remove_vcpkg_linux.sh --apply --purge-shell
```

### 4. Build and start the Rust bot

```bash
cargo build --release
./target/release/linkembed
```

After the first run creates `target/release/config/config.json`, edit the token and restart:

```bash
./target/release/linkembed
```

### 5. Optional: PM2 for the Rust bot

```bash
pm2 start ecosystem.config.js
pm2 save
```

## License

This project is licensed under `LICENSE.txt`.
