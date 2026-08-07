#!/usr/bin/env bash
set -euo pipefail

lang=$1
node=$2
output_directory=$3

rm -rf "$output_directory"
"$lang" project build-web \
    packages/aster_web/aster.toml "$output_directory" browser_http_server
"$node" tests/aster_web_browser_async_integration.mjs \
    "$output_directory/browser_http_server.wasm"
python3 tests/aster_web_browser.py "$output_directory"
