#!/usr/bin/env bash
set -euo pipefail

example_dir=$(cd "$(dirname "$0")" && pwd)
aster_root=$(cd "$example_dir/../.." && pwd)
build_dir="$example_dir/build"
dist_dir="$example_dir/dist"

mkdir -p "$build_dir" "$dist_dir"

"$aster_root/build/lang" project build-web \
    "$example_dir/aster.toml" "$dist_dir" counter

cc -std=c17 -O2 "$dist_dir/counter-server.c" \
    -o "$build_dir/counter-server"
server_html=$($build_dir/counter-server)
{
    printf '%s\n' '<!doctype html>'
    printf '%s\n' '<html lang="en"><head><meta charset="utf-8">'
    printf '%s\n' '<meta name="viewport" content="width=device-width">'
    printf '%s\n' '<title>Aster Wasm counter</title></head><body>'
    printf '%s\n' "$server_html"
    printf '%s\n' '<script type="module" src="./counter.js"></script>'
    printf '%s\n' '</body></html>'
} > "$dist_dir/index.html"

node "$example_dir/verify.mjs"
wc -c "$dist_dir/counter.wasm" "$dist_dir/aster.js" \
    "$dist_dir/counter.js" "$dist_dir/index.html"
