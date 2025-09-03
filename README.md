# LinkEmbed Bot

A C++ Discord bot that automatically detects URLs in messages and replaces them with rich metadata embeds, similar to how Discord handles links from major sites.

Aren't you getting annoyed by all those URLs that won't embed?

## Features

- **Automatic URL Detection**: Monitors messages for URLs from any domain.
- **Rich Metadata Fetching**: Extracts title, description, thumbnail image, and site name from the URL's HTML.
- **Embed Generation**: Creates Discord embeds using the fetched metadata.
- **Anti-Spam Logic**:
    - Waits a few seconds before posting an embed to see if Discord generates its own.
    - If Discord creates an embed, the bot cancels its job.
    - If the original message is edited to remove the URL, the bot's embed is also removed (if already posted).
- **Performance & Scalability**:
    - Utilizes a thread pool for concurrent metadata fetching.
    - Built-in rate limiting to avoid being blocked by target websites.
    - In-memory caching for frequently linked URLs to provide instant embeds and reduce fetching.



## Dependencies

This project is built using `vcpkg` for C++ dependency management.

- [DPP (D++ Library)](https://dpp.dev/): A lightweight C++ library for interacting with the Discord API.
- [libcurl](https://curl.se/libcurl/): For making HTTP requests to fetch HTML content.
- [nlohmann/json](https://github.com/nlohmann/json): For parsing and creating `config.json`.
- Visual Studio 2022 (Optional / For Windows)

## Building the Project

### Prerequisites

- CMake (version 3.22 or higher)
- Ninja: The build presets use the Ninja build system.
  - **Linux**: Install via your package manager (e.g., `sudo apt install ninja-build` on Debian/Ubuntu).
  - **Windows**: Ninja is included with the "C++ CMake tools for Windows" component in the Visual Studio Installer.
    - If you don't have VS, you need to install Ninja manually.
- A modern C++ compiler (e.g., Visual Studio 2022 on Windows, GCC/Clang on Linux)
- [vcpkg](https://github.com/microsoft/vcpkg)

### 1. Clone the Repository

```bash
git clone https://github.com/Ayumudayo/LinkEmbed.git
cd LinkEmbed
```

### 2. Setup vcpkg

This project uses `vcpkg.json` to declare its dependencies. `vcpkg` will automatically install them when you run the CMake configuration step.

- **On Windows:** The `CMakePresets.json` file is configured to find `vcpkg` at the default Visual Studio installation path. If your path is different, please modify the preset file.
- **On Linux:** You must set the `VCPKG_ROOT` environment variable to point to your `vcpkg` installation directory.
  ```bash
  export VCPKG_ROOT=/path/to/your/vcpkg
  # Add this line to your ~/.bashrc or ~/.zshrc to make it permanent
  ```

### 3. Configure and Build using Presets

This project uses `CMakePresets.json` for easy configuration and building. You can configure and build from the command line or directly from a compatible IDE like Visual Studio.

```bash
# 1. Configure the project using a preset (e.g., for Windows Release)
cmake --preset windows-x64-release

# 2. Build the project using the same preset
cmake --build --preset windows-x64-release
```

**Available Presets:**
- `windows-x64-release` (Recommended for Windows)
- `windows-x64-debug`
- `linux-x64-release` (Recommended for Linux)
- `linux-x64-debug`

The final executable will be located in the `out/build/<preset-name>/` directory.

## Configuration

The bot requires a configuration file located at `config/config.json` next to the executable (the app looks up `./config/config.json` relative to the binary).

If the file does not exist on first run, the bot will create a default `config.json` for you and exit. Edit it and restart.

**You must edit this file and set your bot token.** You can also control logging verbosity via `log_level`.

```json
{
    "bot_token": "YOUR_BOT_TOKEN_HERE",
    "cache_ttl_minutes": 10,
    "embed_delay_seconds": 5,
    "html_initial_range_bytes": 524288,
    "html_range_growth_factor": 2,
    "http_max_redirects": 5,
    "http_timeout_ms": 4000,
    "http_user_agent": "LinkEmbedBot/1.0",
    "max_concurrency": 4,
    "max_html_bytes": 8388608,
    "rate_per_sec": 2,
    "log_level": "info"
}
```
- `bot_token`: Your Discord bot's token.
- `embed_delay_seconds`: Time to wait before posting an embed, to allow Discord to create its own first.
- `cache_ttl_minutes`: How long to cache website metadata.
- `log_level`: One of `debug`, `info`, `warn`, `error`.

### Logging

- Console: Logs are printed to stdout with timestamp and level.
- Files: Logs are also written to `logs/YYYY-MM-DD.log` in the executable directory.
- Rotation: The logger automatically switches to a new file at midnight (based on local time).

## Running the Bot

After building the project, you can run the bot directly from the project root:

```bash
# On Windows
.\build\Release\LinkEmbed.exe

# On Linux
./build/LinkEmbed
```

## License

This project is licensed under the terms of the `LICENSE.txt` file.
