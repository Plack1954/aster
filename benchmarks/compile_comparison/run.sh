#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repository"

for tool in cmake hyperfine go rustc cargo jq; do
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
whole_program_flag=
if printf 'int main(void) { return 0; }\n' |
        "$cc_bin" -x c -fwhole-program -o /dev/null - >/dev/null 2>&1; then
    whole_program_flag=-fwhole-program
fi

runs=${ASTER_COMPILE_BENCH_RUNS:-20}
build_dir=${ASTER_COMPILE_BENCH_BUILD:-build-compile-comparison}
compiler_build=${ASTER_COMPILE_BENCH_COMPILER_BUILD:-build-rust-comparison}
output_dir="$build_dir/results"
aster_source=benchmarks/rust_comparison/html.lang
go_source=benchmarks/go_comparison/html.go
rust_source=benchmarks/rust_comparison/html.rs
sailfish_manifest=benchmarks/rust_comparison/sailfish_html/Cargo.toml
sailfish_target="$build_dir/sailfish-target"
sailfish_package=aster-sailfish-html-benchmark
sailfish_executable="$sailfish_target/release/$sailfish_package"

cmake -S . -B "$compiler_build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$compiler_build" -j 4
mkdir -p "$output_dir"
cargo build --quiet --release --locked \
    --manifest-path "$sailfish_manifest" --target-dir "$sailfish_target"

aster_compiler="$compiler_build/lang"
aster_c="$output_dir/html.aster.c"
aster_runtime_c="$output_dir/aster_runtime.c"
aster_runtime_object="$output_dir/aster_runtime.o"
aster_dev="$output_dir/html_aster_dev"
aster_release="$output_dir/html_aster_release"
go_executable="$output_dir/html_go"
rust_executable="$output_dir/html_rust"

"$aster_compiler" emit-c-runtime > "$aster_runtime_c"
"$cc_bin" -std=c17 -O3 -DNDEBUG -c \
    "$aster_runtime_c" -o "$aster_runtime_object"

hyperfine --shell=none --warmup 3 --runs "$runs" \
    --export-json "$output_dir/aster_frontend.json" \
    -n aster-frontend "$aster_compiler emit-c $aster_source"

hyperfine --warmup 2 --runs "$runs" \
    --export-json "$output_dir/executables.json" \
    -n aster-dev \
    "$aster_compiler emit-c $aster_source > $aster_c && $cc_bin -std=c17 -O0 -DNDEBUG -DASTER_EXTERNAL_RUNTIME $aster_c $aster_runtime_object -o $aster_dev" \
    -n aster-release \
    "$aster_compiler emit-c $aster_source > $aster_c && $cc_bin -std=c17 -O3 -DNDEBUG $whole_program_flag -DASTER_EXTERNAL_RUNTIME $aster_c $aster_runtime_object -o $aster_release" \
    -n go-recompile-main \
    "go build -gcflags=command-line-arguments=-D=nonce\$(date +%s%N) -o $go_executable $go_source" \
    -n rust-handwritten \
    "rustc -C opt-level=3 -C debuginfo=0 -o $rust_executable $rust_source"

hyperfine --warmup 2 --runs "$runs" \
    --export-json "$output_dir/go_cached.json" \
    -n go-cached "go build -o $go_executable $go_source"

hyperfine --warmup 2 --runs "$runs" \
    --prepare "cargo clean --quiet --release -p $sailfish_package --manifest-path $sailfish_manifest --target-dir $sailfish_target" \
    --export-json "$output_dir/sailfish.json" \
    -n rust-sailfish \
    "cargo build --quiet --release --locked --manifest-path $sailfish_manifest --target-dir $sailfish_target"

"$aster_dev" > "$output_dir/aster-dev.out"
"$aster_release" > "$output_dir/aster-release.out"
"$go_executable" > "$output_dir/go.out"
"$rust_executable" > "$output_dir/rust.out"
"$sailfish_executable" > "$output_dir/sailfish.out"
cmp "$output_dir/aster-release.out" "$output_dir/aster-dev.out"
cmp "$output_dir/aster-release.out" "$output_dir/go.out"
cmp "$output_dir/aster-release.out" "$output_dir/rust.out"
cmp "$output_dir/aster-release.out" "$output_dir/sailfish.out"

printf '\nMedian wall times\n'
for result in "$output_dir"/*.json; do
    jq -r --arg file "$(basename "$result" .json)" \
        '.results[] | [$file, .command, (.median * 1000 | tostring + " ms")] | @tsv' \
        "$result"
done

printf '\nGenerated C size\n'
wc -l -c "$aster_c"
printf '\nRaw results: %s\n' "$output_dir"
