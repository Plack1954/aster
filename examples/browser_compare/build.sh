#!/usr/bin/env bash
set -euo pipefail

example_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$example_dir/../.." && pwd)
lang=${1:-"$root/build/lang"}
build_dir="$example_dir/build"
dist_dir="$example_dir/dist"

rm -rf "$build_dir" "$dist_dir"
mkdir -p "$build_dir" "$dist_dir"
"$lang" project build-web "$example_dir/aster.toml" "$dist_dir" compare
cc -std=c17 -O2 "$dist_dir/compare-server.c" -o "$build_dir/server"
{
    printf '%s\n' '<!doctype html><meta charset="utf-8"><title>Aster comparison</title>'
    "$build_dir/server"
    printf '%s\n' '<script type="module" src="./compare.js"></script>'
} >"$dist_dir/aster.html"

curl -fsSL \
    https://unpkg.com/vue@3.5.41/dist/vue.runtime.esm-browser.prod.js \
    -o "$dist_dir/vue.js"
cp "$example_dir/vue-app.js" "$example_dir/vue.html" "$dist_dir/"

node "$example_dir/verify.mjs" "$dist_dir/compare.wasm"
python3 "$example_dir/benchmark.py" "$dist_dir"

aster_raw=$(wc -c <"$dist_dir/compare.wasm")
aster_raw=$((aster_raw + $(wc -c <"$dist_dir/aster.js") + $(wc -c <"$dist_dir/compare.js")))
vue_raw=$(wc -c <"$dist_dir/vue.js")
vue_raw=$((vue_raw + $(wc -c <"$dist_dir/vue-app.js")))
aster_gzip=$({ gzip -c -9 "$dist_dir/compare.wasm"; gzip -c -9 "$dist_dir/aster.js"; gzip -c -9 "$dist_dir/compare.js"; } | wc -c)
vue_gzip=$({ gzip -c -9 "$dist_dir/vue.js"; gzip -c -9 "$dist_dir/vue-app.js"; } | wc -c)
printf 'Aster client: %d bytes raw, %d bytes gzip\n' "$aster_raw" "$aster_gzip"
printf 'Vue client:   %d bytes raw, %d bytes gzip\n' "$vue_raw" "$vue_gzip"
