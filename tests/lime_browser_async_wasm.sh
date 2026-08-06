#!/usr/bin/env bash
set -euo pipefail

lang=$1
node=$2
output_directory=$3

rm -rf "$output_directory"
"$lang" project build-web \
    packages/lime/aster.toml "$output_directory" browser_http_server
"$node" tests/lime_browser_async_integration.mjs \
    "$output_directory/browser_http_server.wasm"
