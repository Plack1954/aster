#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repository"
nim_bin=${NIM:-nim}
nim_args=
if ! command -v "$nim_bin" >/dev/null 2>&1; then
    if [ -x build-nim-2/nim-2.2.10/bin/nim ]; then
        nim_bin=build-nim-2/nim-2.2.10/bin/nim
    else
        nim_bin=build-nim-toolchain/root/usr/bin/nim
        [ -x "$nim_bin" ] || { echo "error: Nim is required" >&2; exit 2; }
        nim_args="--lib:build-nim-toolchain/root/usr/lib/nim/lib --path:build-nim-toolchain/root/usr/lib/nim/lib/core --path:build-nim-toolchain/root/usr/lib/nim/lib/pure --path:build-nim-toolchain/root/usr/lib/nim/lib/pure/collections --path:build-nim-toolchain/root/usr/lib/nim/lib/impure --path:build-nim-toolchain/root/usr/lib/nim/lib/wrappers --path:build-nim-toolchain/root/usr/lib/nim/lib/posix"
    fi
fi
nim_mode=${NIM_MODE:-release}
nim_mm=${NIM_MM:-arc}
for tool in cmake hyperfine; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool is required" >&2; exit 2; }
done
cc_bin=${CC:-cc}
runs=${ASTER_NIM_COMPILE_RUNS:-20}
build_dir=${ASTER_NIM_BENCH_BUILD:-build-nim-comparison}
results="$build_dir/compile-results"
mkdir -p "$results" "$build_dir/nimcache/compile"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j 4
compiler="$build_dir/lang"
runtime_c="$results/runtime.c"
runtime_o="$results/runtime.o"
aster_c="$results/html.c"
nim_source="$results/html.nim"
"$compiler" emit-c-runtime > "$runtime_c"
"$cc_bin" -std=c17 -O3 -c "$runtime_c" -o "$runtime_o"
cp benchmarks/nim_comparison/html.nim "$nim_source"
"$nim_bin" c $nim_args -d:"$nim_mode" --mm:"$nim_mm" --opt:speed --hints:off --warnings:off \
    --nimcache:"$build_dir/nimcache/compile" -o:"$results/html_nim" "$nim_source" >/dev/null

whole_program_flag=
printf 'int main(void){return 0;}\n' | "$cc_bin" -x c -fwhole-program -o /dev/null - >/dev/null 2>&1 && whole_program_flag=-fwhole-program
hyperfine --warmup 2 --runs "$runs" --export-json "$results/compile.json" \
    -n aster-dev "$compiler emit-c benchmarks/rust_comparison/html.lang > $aster_c && $cc_bin -std=c17 -O0 -DNDEBUG -DASTER_EXTERNAL_RUNTIME $aster_c $runtime_o -o $results/html_aster_dev" \
    -n aster-release "$compiler emit-c benchmarks/rust_comparison/html.lang > $aster_c && $cc_bin -std=c17 -O3 -DNDEBUG $whole_program_flag -DASTER_EXTERNAL_RUNTIME $aster_c $runtime_o -o $results/html_aster_release" \
    -n nim-release "touch $nim_source && $nim_bin c $nim_args -d:$nim_mode --mm:$nim_mm --opt:speed --hints:off --warnings:off --nimcache:$build_dir/nimcache/compile -o:$results/html_nim $nim_source >/dev/null"
