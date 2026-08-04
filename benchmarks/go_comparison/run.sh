#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repository"

for tool in cmake hyperfine go ab curl jq; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: $tool is required" >&2
        exit 2
    fi
done

cc_bin=${CC:-cc}
if ! command -v "$cc_bin" >/dev/null 2>&1; then
    echo "error: C compiler '$cc_bin' was not found" >&2
    exit 2
fi

build_dir=${ASTER_GO_BENCH_BUILD:-build-go-comparison}
runs=${ASTER_GO_BENCH_RUNS:-10}
http_runs=${HTTP_RUNS:-5}
http_requests=${HTTP_REQUESTS:-2000}
output_dir="$build_dir/results"
source_dir=benchmarks/go_comparison

cmake -S . -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$cc_bin"
cmake --build "$build_dir" -j 4
mkdir -p "$output_dir"

{
    date '+%Y-%m-%dT%H:%M:%S%z'
    uname -a
    "$cc_bin" --version | head -1
    go version
    go env GOOS GOARCH GOAMD64
} > "$output_dir/environment.txt"

for name in logic functions strings html; do
    aster_source="$source_dir/$name.lang"
    go_source="$source_dir/$name.go"
    generated_c="$output_dir/$name.c"
    aster_executable="$output_dir/${name}_aster_c"
    go_executable="$output_dir/${name}_go"

    "$build_dir/lang" emit-c "$aster_source" > "$generated_c"
    "$cc_bin" -std=c17 -O2 -DNDEBUG \
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        "$generated_c" -o "$aster_executable"
    go build -trimpath -o "$go_executable" "$go_source"

    "$aster_executable" > "$output_dir/$name.aster-c.out"
    "$go_executable" > "$output_dir/$name.go.out"
    "$build_dir/lang" run-ir "$aster_source" \
        > "$output_dir/$name.aster-vm.out"
    cmp "$output_dir/$name.aster-c.out" "$output_dir/$name.go.out"
    cmp "$output_dir/$name.aster-c.out" "$output_dir/$name.aster-vm.out"

    hyperfine --shell=none --warmup 2 --runs "$runs" \
        --export-json "$output_dir/$name.json" \
        -n aster-c "$aster_executable" \
        -n go "$go_executable" \
        -n aster-vm "$build_dir/lang run-ir $aster_source"
done

aster_http_c="$output_dir/http_server.c"
aster_http_executable="$output_dir/http_server_aster_c"
go_http_executable="$output_dir/http_server_go"
"$build_dir/lang" emit-c "$source_dir/http_server.lang" \
    > "$aster_http_c"
"$cc_bin" -std=c17 -O2 -DNDEBUG -I include \
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror \
    "$aster_http_c" "$build_dir/liblanglib.a" -lsqlite3 \
    -o "$aster_http_executable"
go build -trimpath -o "$go_http_executable" \
    "$source_dir/http_server.go"

aster_pid=
go_pid=
cleanup() {
    if [ -n "$aster_pid" ]; then
        kill "$aster_pid" 2>/dev/null || true
        wait "$aster_pid" 2>/dev/null || true
    fi
    if [ -n "$go_pid" ]; then
        kill "$go_pid" 2>/dev/null || true
        wait "$go_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

"$aster_http_executable" > "$output_dir/http-aster.log" 2>&1 &
aster_pid=$!
"$go_http_executable" > "$output_dir/http-go.log" 2>&1 &
go_pid=$!

wait_for_url() {
    url=$1
    attempts=0
    while [ "$attempts" -lt 100 ]; do
        if curl --silent --show-error --fail "$url" >/dev/null 2>&1; then
            return 0
        fi
        attempts=$((attempts + 1))
        sleep 0.05
    done
    echo "error: server did not become ready at $url" >&2
    return 1
}

aster_url=http://127.0.0.1:18280/benchmark
go_url=http://127.0.0.1:18281/benchmark
wait_for_url "$aster_url"
wait_for_url "$go_url"
curl --silent --show-error --fail "$aster_url" \
    > "$output_dir/http.aster-c.out"
curl --silent --show-error --fail "$go_url" \
    > "$output_dir/http.go.out"
cmp "$output_dir/http.aster-c.out" "$output_dir/http.go.out"

hyperfine --warmup 1 --runs "$http_runs" \
    --export-json "$output_dir/http.json" \
    -n aster-http \
    "ab -q -n $http_requests -c 1 $aster_url" \
    -n go-http \
    "ab -q -n $http_requests -c 1 $go_url"

printf '\nMedian wall times\n'
for result in "$output_dir"/*.json; do
    jq -r --arg file "$(basename "$result" .json)" \
        '.results[] | [$file, .command, (.median * 1000 | tostring + " ms")] | @tsv' \
        "$result"
done

printf '\nRaw results: %s\n' "$output_dir"
