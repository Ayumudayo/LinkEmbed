#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e

# Get the directory of the script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/.."

# Navigate to the project root
cd "$PROJECT_ROOT"

"$PROJECT_ROOT/scripts/check_linux_env.sh" || {
  echo ">>> Environment check failed. Please fix the issues above and retry."
  exit 1
}

echo ">>> Configuring Debug build for linux-x64-debug..."
cmake --preset linux-x64-debug

echo ">>> Building Debug..."
cmake --build --preset linux-x64-debug

echo ">>> Build complete. Executable is at: build/linux-x64-debug/LinkEmbed"
