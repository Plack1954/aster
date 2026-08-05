#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repository"

# Benchmark builds and executions inherit the same hard memory boundary used
# by the repository test workflow. Set ASTER_BENCH_BOUNDED=1 only when an
# equivalent outer boundary is already active.
if [ "${ASTER_BENCH_BOUNDED:-0}" != 1 ] && \
   command -v systemd-run >/dev/null 2>&1; then
    exec systemd-run --user --pipe --wait --quiet \
        --working-directory="$repository" \
        -p MemoryMax="${ASTER_BENCH_MEMORY_MAX:-2G}" \
        -p MemorySwapMax=0 \
        env ASTER_BENCH_BOUNDED=1 "$0" "$@"
fi

for tool in cmake hyperfine jq cmp; do
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

build_dir=${ASTER_CEILING_BENCH_BUILD:-build-performance-ceiling}
runs=${ASTER_CEILING_BENCH_RUNS:-10}
output_dir="$build_dir/results"
source_dir=benchmarks/performance_ceiling

cmake -S . -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$cc_bin" \
    -DASTER_ENABLE_SQLITE_TESTS=OFF
cmake --build "$build_dir" -j "${ASTER_BENCH_JOBS:-2}"
mkdir -p "$output_dir"

{
    date '+%Y-%m-%dT%H:%M:%S%z'
    uname -a
    "$cc_bin" --version | head -1
} > "$output_dir/environment.txt"

for name in arithmetic function_calls list_growth owned_strings; do
    aster_source="$source_dir/$name.as"
    ceiling_source="$source_dir/$name.c"
    generated_c="$output_dir/$name.generated.c"
    aster_executable="$output_dir/$name.aster-c"
    ceiling_executable="$output_dir/$name.ceiling-c"

    "$build_dir/lang" emit-c "$aster_source" > "$generated_c"
    "$cc_bin" -std=c17 -O3 -DNDEBUG -march=native \
        "$generated_c" -o "$aster_executable"
    "$cc_bin" -std=c17 -O3 -DNDEBUG -march=native \
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        "$ceiling_source" -o "$ceiling_executable"

    "$aster_executable" > "$output_dir/$name.aster-c.out"
    "$ceiling_executable" > "$output_dir/$name.ceiling-c.out"
    "$build_dir/lang" run-ir "$aster_source" \
        > "$output_dir/$name.aster-vm.out"
    cmp "$output_dir/$name.ceiling-c.out" \
        "$output_dir/$name.aster-c.out"
    cmp "$output_dir/$name.ceiling-c.out" \
        "$output_dir/$name.aster-vm.out"

    hyperfine --shell=none --warmup 2 --runs "$runs" \
        --export-json "$output_dir/$name.json" \
        -n ceiling-c "$ceiling_executable" \
        -n aster-c "$aster_executable" \
        -n aster-vm "$build_dir/lang run-ir $aster_source"
done

printf '\nMedian wall time and distance from the C floor\n'
for result in "$output_dir"/*.json; do
    jq -r --arg workload "$(basename "$result" .json)" '
        (.results[] | select(.command == "ceiling-c") | .median) as $floor
        | .results[]
        | [$workload, .command,
           ((.median * 1000) | tostring + " ms"),
           ((.median / $floor) | tostring + "x")]
        | @tsv
    ' "$result"
done

printf '\nRaw results: %s\n' "$output_dir"
