#!/usr/bin/env bash
set -euo pipefail
lang=$1
node=$2
output_directory=$3
rm -rf "$output_directory"
"$lang" project build-web \
    examples/browser_compare/BrowserCompare.asproj "$output_directory"
"$node" examples/browser_compare/verify.mjs \
    "$output_directory/BrowserCompare.wasm"
