#!/usr/bin/env bash
set -euo pipefail

lang=$1
c_compiler=$2
runtime_library=$3
http_client_library=$4
sqlite_library=$5
curl_library=$6
python=$7
output_directory=$8

mkdir -p "$output_directory"
port_file=$(mktemp)
server_pid=""
cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -f "$port_file"
}
trap cleanup EXIT

"$python" tests/http_client_fixture.py > "$port_file" &
server_pid=$!
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    [[ -s "$port_file" ]] && break
    sleep 0.1
done
test -s "$port_file"
port=$(head -n 1 "$port_file")
origin="http://127.0.0.1:${port}"

ASTER_HTTP_TEST_ORIGIN="$origin" \
    "$lang" run tests/http_client_surface.as
"$lang" emit-c tests/http_client_surface.as > "$output_directory/client.c"

libraries=("$http_client_library" "$runtime_library")
if [[ -n "$sqlite_library" ]]; then libraries+=("$sqlite_library"); fi
if [[ -n "$curl_library" ]]; then libraries+=("$curl_library"); fi
"$c_compiler" \
    -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror \
    -I include "$output_directory/client.c" "${libraries[@]}" \
    -o "$output_directory/client"
ASTER_HTTP_TEST_ORIGIN="$origin" "$output_directory/client"
