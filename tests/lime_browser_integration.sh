#!/usr/bin/env bash
set -euo pipefail

lang=$1
c_compiler=$2
runtime_library=$3
sqlite_library=$4
output_directory=$5
sanitize=${6:-off}

mkdir -p "$output_directory"
"$lang" project build-web \
    packages/lime/aster.toml "$output_directory" browser_http_server

compile_flags=(
    -std=c17
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Werror
    -I include
)
if [[ "$sanitize" == on ]]; then
    compile_flags+=(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
fi
"$c_compiler" "${compile_flags[@]}" \
    "$output_directory/browser_http_server-server.c" \
    "$runtime_library" "$sqlite_library" \
    -o "$output_directory/browser-server"

port_file=$(mktemp)
server_pid=""
cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
    fi
    rm -f "$port_file"
}
trap cleanup EXIT

ASTER_BROWSER_ASSET_DIR="$output_directory" \
    "$output_directory/browser-server" > "$port_file" &
server_pid=$!
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    [[ -s "$port_file" ]] && break
    sleep 0.1
done
port=$(head -n 1 "$port_file")

curl -fsS "http://127.0.0.1:${port}/" |
    grep -q 'data-aster-event="click|Increment|l|l:count"'
test "$(curl -fsS \
    "http://127.0.0.1:${port}/browser/browser_http_server.wasm" |
    wc -c)" -gt 0
curl -fsS "http://127.0.0.1:${port}/browser/aster.js" |
    grep -q 'hydrateAster'
curl -fsS \
    "http://127.0.0.1:${port}/browser/browser_http_server.js" |
    grep -q 'browser_http_server.wasm'
curl -fsS -X POST \
    -H 'Content-Type: application/x-www-form-urlencoded' \
    --data 'name=Brandon' \
    "http://127.0.0.1:${port}/contact" |
    grep -q 'Saved without WebAssembly'

wait "$server_pid"
server_pid=""
