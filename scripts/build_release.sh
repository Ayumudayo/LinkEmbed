#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

cargo build --release

echo ""
echo "Build complete:"
echo "  $PROJECT_ROOT/target/release/linkembed"
