#!/usr/bin/env bash
set -euo pipefail

lang=$1
node=$2
output_directory=$3

rm -rf "$output_directory"
"$lang" project build-web \
    packages/aster_web/BrowserHttpServer.asproj "$output_directory"
"$node" tests/aster_web_browser_async_integration.mjs \
    "$output_directory/Aster.Web.BrowserHttpServer.wasm"
python3 tests/aster_web_browser.py "$output_directory"
