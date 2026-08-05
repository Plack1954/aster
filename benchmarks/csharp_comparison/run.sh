#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repository"

if [ "${ASTER_BENCH_BOUNDED:-0}" != 1 ] && \
   command -v systemd-run >/dev/null 2>&1; then
    exec systemd-run --user --pipe --wait --quiet \
        --working-directory="$repository" \
        -p MemoryMax="${ASTER_BENCH_MEMORY_MAX:-2G}" \
        -p MemorySwapMax=0 \
        env ASTER_BENCH_BOUNDED=1 \
            ASTER_CSHARP_WORKLOADS="${ASTER_CSHARP_WORKLOADS:-}" \
            ASTER_CSHARP_BENCH_BUILD="${ASTER_CSHARP_BENCH_BUILD:-}" \
            ASTER_CSHARP_BENCH_RUNS="${ASTER_CSHARP_BENCH_RUNS:-}" \
            ASTER_CSHARP_INCLUDE_VM="${ASTER_CSHARP_INCLUDE_VM:-}" \
            ASTER_CSHARP_INCLUDE_HTTP="${ASTER_CSHARP_INCLUDE_HTTP:-}" \
            ASTER_CSHARP_HTTP_RUNS="${ASTER_CSHARP_HTTP_RUNS:-}" \
            ASTER_CSHARP_HTTP_REQUESTS="${ASTER_CSHARP_HTTP_REQUESTS:-}" \
            ASTER_BENCH_JOBS="${ASTER_BENCH_JOBS:-}" \
            CC="${CC:-}" \
            "$0" "$@"
fi

for tool in cmake dotnet hyperfine jq cmp; do
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

all_workloads="arithmetic function_calls branches list_growth list_scan dictionary hash_set queue owned_strings string_builder text_search json_parse exceptions html_render"
workloads=${ASTER_CSHARP_WORKLOADS:-$all_workloads}
build_dir=${ASTER_CSHARP_BENCH_BUILD:-build-csharp-comparison}
runs=${ASTER_CSHARP_BENCH_RUNS:-10}
include_vm=${ASTER_CSHARP_INCLUDE_VM:-0}
include_http=${ASTER_CSHARP_INCLUDE_HTTP:-1}
http_runs=${ASTER_CSHARP_HTTP_RUNS:-5}
http_requests=${ASTER_CSHARP_HTTP_REQUESTS:-5000}
output_dir="$build_dir/results"
csharp_project=benchmarks/csharp_comparison/csharp/AsterCSharpBenchmarks.csproj
csharp_build="$build_dir/csharp"

cmake -S . -B "$build_dir/aster" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$cc_bin" \
    -DASTER_ENABLE_SQLITE_TESTS=OFF
cmake --build "$build_dir/aster" --target lang \
    -j "${ASTER_BENCH_JOBS:-2}"

DOTNET_CLI_TELEMETRY_OPTOUT=1 \
DOTNET_NOLOGO=1 \
dotnet publish "$csharp_project" \
    --configuration Release \
    --output "$csharp_build" \
    --artifacts-path "$build_dir/dotnet-artifacts" \
    --nologo

mkdir -p "$output_dir"
aster_lang="$build_dir/aster/lang"
csharp_executable="$csharp_build/AsterCSharpBenchmarks"

{
    date '+%Y-%m-%dT%H:%M:%S%z'
    uname -a
    "$cc_bin" --version | head -1
    dotnet --version
} > "$output_dir/environment.txt"

for name in $workloads; do
    case " $all_workloads " in
        *" $name "*) ;;
        *) echo "error: unknown workload '$name'" >&2; exit 2 ;;
    esac

    source="benchmarks/csharp_comparison/$name.as"
    generated_c="$output_dir/$name.generated.c"
    aster_executable="$output_dir/$name.aster-c"

    "$aster_lang" emit-c "$source" > "$generated_c"
    "$cc_bin" -std=c17 -O3 -DNDEBUG -march=native \
        "$generated_c" -o "$aster_executable"

    "$aster_executable" > "$output_dir/$name.aster-c.out"
    DOTNET_TieredCompilation=0 \
        "$csharp_executable" "$name" > "$output_dir/$name.csharp.out"
    cmp "$output_dir/$name.aster-c.out" "$output_dir/$name.csharp.out"

    if [ "$include_vm" = 1 ]; then
        "$aster_lang" run-ir "$source" > "$output_dir/$name.aster-vm.out"
        cmp "$output_dir/$name.aster-c.out" "$output_dir/$name.aster-vm.out"
        hyperfine --shell=none --warmup 2 --runs "$runs" \
            --export-json "$output_dir/$name.json" \
            -n csharp-jit \
                "env DOTNET_TieredCompilation=0 $csharp_executable $name" \
            -n aster-c "$aster_executable" \
            -n aster-vm "$aster_lang run-ir $source"
    else
        hyperfine --shell=none --warmup 2 --runs "$runs" \
            --export-json "$output_dir/$name.json" \
            -n csharp-jit \
                "env DOTNET_TieredCompilation=0 $csharp_executable $name" \
            -n aster-c "$aster_executable"
    fi
done

if [ "$include_http" = 1 ]; then
    for tool in ab curl; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "error: $tool is required for the HTTP comparison" >&2
            exit 2
        fi
    done

    http_source=benchmarks/csharp_comparison/http_server.as
    http_generated="$output_dir/http_server.generated.c"
    aster_http="$output_dir/http_server.aster-c"
    "$aster_lang" emit-c "$http_source" > "$http_generated"
    "$cc_bin" -std=c17 -O3 -DNDEBUG -march=native -I include \
        "$http_generated" "$build_dir/aster/liblanglib.a" -lsqlite3 \
        -o "$aster_http"

    aster_pid=
    csharp_pid=
    cleanup_servers() {
        if [ -n "$aster_pid" ]; then
            kill "$aster_pid" 2>/dev/null || true
            wait "$aster_pid" 2>/dev/null || true
        fi
        if [ -n "$csharp_pid" ]; then
            kill "$csharp_pid" 2>/dev/null || true
            wait "$csharp_pid" 2>/dev/null || true
        fi
    }
    trap cleanup_servers EXIT INT TERM

    "$aster_http" > "$output_dir/http.aster.log" 2>&1 &
    aster_pid=$!
    DOTNET_TieredCompilation=0 \
        "$csharp_executable" http > "$output_dir/http.csharp.log" 2>&1 &
    csharp_pid=$!

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

    aster_url=http://127.0.0.1:18480/benchmark
    csharp_url=http://127.0.0.1:18481/benchmark
    wait_for_url "$aster_url"
    wait_for_url "$csharp_url"
    curl --silent --show-error --fail "$aster_url" \
        > "$output_dir/http.aster.out"
    curl --silent --show-error --fail "$csharp_url" \
        > "$output_dir/http.csharp.out"
    cmp "$output_dir/http.aster.out" "$output_dir/http.csharp.out"

    hyperfine --warmup 1 --runs "$http_runs" \
        --export-json "$output_dir/http.json" \
        -n csharp-http "ab -q -n $http_requests -c 1 $csharp_url" \
        -n aster-http "ab -q -n $http_requests -c 1 $aster_url"

    cleanup_servers
    trap - EXIT INT TERM
fi

printf '\nMedian wall time relative to C# (lower is faster)\n'
for name in $workloads; do
    result="$output_dir/$name.json"
    jq -r --arg workload "$name" '
        (.results[] | select(.command == "csharp-jit") | .median) as $csharp
        | .results[]
        | [$workload, .command,
           ((.median * 1000) | tostring + " ms"),
           ((.median / $csharp) | tostring + "x C#")]
        | @tsv
    ' "$result"
done

if [ "$include_http" = 1 ]; then
    jq -r '
        (.results[] | select(.command == "csharp-http") | .median) as $csharp
        | .results[]
        | ["http", .command,
           ((.median * 1000) | tostring + " ms"),
           ((.median / $csharp) | tostring + "x C#")]
        | @tsv
    ' "$output_dir/http.json"
fi

printf '\nRaw results: %s\n' "$output_dir"
