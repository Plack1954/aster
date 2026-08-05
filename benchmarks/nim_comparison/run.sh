#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repository"

nim_bin=${NIM:-nim}
nim_lib_arg=
nim_path_args=
if ! command -v "$nim_bin" >/dev/null 2>&1; then
    if [ -x build-nim-2/nim-2.2.10/bin/nim ]; then
        nim_bin=build-nim-2/nim-2.2.10/bin/nim
    elif [ -x build-nim-toolchain/root/usr/bin/nim ]; then
        nim_bin=build-nim-toolchain/root/usr/bin/nim
        nim_lib_arg=--lib:build-nim-toolchain/root/usr/lib/nim/lib
        nim_path_args="--path:build-nim-toolchain/root/usr/lib/nim/lib/core --path:build-nim-toolchain/root/usr/lib/nim/lib/pure --path:build-nim-toolchain/root/usr/lib/nim/lib/pure/collections --path:build-nim-toolchain/root/usr/lib/nim/lib/impure --path:build-nim-toolchain/root/usr/lib/nim/lib/wrappers --path:build-nim-toolchain/root/usr/lib/nim/lib/posix"
    else
        echo "error: Nim is required" >&2
        exit 2
    fi
fi
nim_mode=${NIM_MODE:-release}
nim_mm=${NIM_MM:-arc}
for tool in cmake hyperfine jq; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: $tool is required" >&2; exit 2;
    }
done
cc_bin=${CC:-cc}
build_dir=${ASTER_NIM_BENCH_BUILD:-build-nim-comparison}
runs=${ASTER_NIM_BENCH_RUNS:-10}
output_dir="$build_dir/results"
source_dir=benchmarks/nim_comparison
nimcache="$build_dir/nimcache"

cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j 4
mkdir -p "$output_dir" "$nimcache"

{
    date '+%Y-%m-%dT%H:%M:%S%z'
    uname -a
    "$cc_bin" --version | head -1
    "$nim_bin" --version
} > "$output_dir/environment.txt"

for name in logic functions strings html; do
    aster_source="benchmarks/rust_comparison/$name.as"
    nim_source="$source_dir/$name.nim"
    generated_c="$output_dir/$name.c"
    aster_executable="$output_dir/${name}_aster_c"
    nim_executable="$output_dir/${name}_nim"
    "$build_dir/lang" emit-c "$aster_source" > "$generated_c"
    "$cc_bin" -std=c17 -O3 -DNDEBUG "$generated_c" -o "$aster_executable"
    "$nim_bin" c $nim_lib_arg $nim_path_args -d:"$nim_mode" --mm:"$nim_mm" --opt:speed --hints:off --warnings:off \
        --nimcache:"$nimcache/$name" -o:"$nim_executable" "$nim_source"
    "$aster_executable" > "$output_dir/$name.aster-c.out"
    "$nim_executable" > "$output_dir/$name.nim.out"
    "$build_dir/lang" run-ir "$aster_source" > "$output_dir/$name.aster-vm.out"
    cmp "$output_dir/$name.aster-c.out" "$output_dir/$name.nim.out"
    cmp "$output_dir/$name.aster-c.out" "$output_dir/$name.aster-vm.out"
    hyperfine --shell=none --warmup 2 --runs "$runs" \
        --export-json "$output_dir/$name.json" \
        -n aster-c "$aster_executable" -n nim "$nim_executable" \
        -n aster-vm "$build_dir/lang run-ir $aster_source"
done

printf '\nMedian wall times\n'
for result in "$output_dir"/*.json; do
    jq -r --arg file "$(basename "$result" .json)" \
        '.results[] | [$file, .command, (.median * 1000 | tostring + " ms")] | @tsv' "$result"
done
