#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repository"

if ! command -v hyperfine >/dev/null 2>&1; then
    echo "error: hyperfine is required" >&2
    exit 2
fi

cc_bin=${CC:-cc}
build_dir=${ASTER_BENCH_BUILD:-build-backend-bench}
runs=${ASTER_BENCH_RUNS:-10}
output_dir="$build_dir/results"

cmake -S . -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$cc_bin"
cmake --build "$build_dir" -j 4
mkdir -p "$output_dir"

for name in backend_loop backend_compute; do
    source="benchmarks/$name.lang"
    "$build_dir/lang" emit-c "$source" > "$output_dir/$name.c"
    "$cc_bin" -std=c17 -O2 -DNDEBUG \
        "$output_dir/$name.c" -o "$output_dir/${name}_c"

    "$build_dir/lang" run-ir "$source"
    "$output_dir/${name}_c"

    hyperfine --shell=none --warmup 2 --runs "$runs" \
        --export-json "$output_dir/$name.json" \
        "$build_dir/lang run-ir $source" \
        "$output_dir/${name}_c"
done

hyperfine --warmup 3 --runs "$runs" \
    --export-json "$output_dir/native_build.json" \
    "$build_dir/lang emit-c benchmarks/backend_compute.lang > $output_dir/timed.c && $cc_bin -std=c17 -O2 -DNDEBUG $output_dir/timed.c -o $output_dir/timed_c"
