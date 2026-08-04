#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repository"

if ! command -v hyperfine >/dev/null 2>&1; then
    echo "error: hyperfine is required" >&2
    exit 2
fi

cc_bin=${CC:-cc}
build_dir=${ASTER_HTTP_BENCH_BUILD:-build-http-bench}
runs=${ASTER_HTTP_BENCH_RUNS:-10}

cmake -S . -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$cc_bin"
cmake --build "$build_dir" -j 4

"$build_dir/lang" project emit-c \
    examples/issue_tracker/aster.toml integration_server \
    > "$build_dir/issue_tracker_integration.c"
"$cc_bin" -std=c17 -O2 -DNDEBUG -I include \
    "$build_dir/issue_tracker_integration.c" \
    "$build_dir/liblanglib.a" -lsqlite3 \
    -o "$build_dir/issue_tracker_integration"

hyperfine --shell=none --warmup 2 --runs "$runs" \
    "$build_dir/generated_http_integration_test vm" \
    "$build_dir/generated_http_integration_test $build_dir/issue_tracker_integration"

wc -c "$build_dir/lang" "$build_dir/issue_tracker_integration"
