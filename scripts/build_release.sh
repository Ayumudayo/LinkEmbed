#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e

# Get the directory of the script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/.."

# Navigate to the project root
cd "$PROJECT_ROOT"

echo ">>> Configuring Release build for linux-x64-release..."
cmake --preset linux-x64-release

echo ">>> Building Release..."
cmake --build --preset linux-x64-release

echo ">>> Build complete. Executable is at: out/build/linux-x64-release/LinkEmbed"
