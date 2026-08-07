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

page=$(curl -fsS "http://127.0.0.1:${port}/")
grep -q 'data-aster-event="click|Increment|l|l:count"' <<<"$page"
grep -q 'data-aster-event="click|IncrementLater|L|l:count"' <<<"$page"
grep -q 'data-aster-event="click|DecreaseReactive|a|l:value"' <<<"$page"
grep -q 'data-aster-event="click|DecreaseProjected|p|l:count"' <<<"$page"
grep -Eq 'data-aster-project="t:[0-9a-f]{16}"' <<<"$page"
grep -Eq 'data-aster-project="d:[0-9a-f]{16}"' <<<"$page"
grep -Eq 'data-aster-project="c:[0-9a-f]{16}"' <<<"$page"
grep -Eq 'data-aster-event="click\|SelectSecondAndRemoveFirst\|p\|l:@[0-9a-f]{16}"' <<<"$page"
! grep -Eq 'data-aster-project="[tdc]:(summary|disabled|className|secondClass)"' <<<"$page"
grep -q 'data-aster-key="native-1"' <<<"$page"
grep -q 'data-aster-event="click|RemoveNativeTodo|h|s:key"' <<<"$page"
grep -q 'data-aster-component="PersistentTodoList"' <<<"$page"
grep -q 'click|PersistentTodoList_AppendTodo|h|x:PersistentTodoList' <<<"$page"
grep -q 'click|PersistentTodoList_RenameTodo|h|x:PersistentTodoList|s:key' <<<"$page"
grep -Eq 'data-aster-part-t="[0-9a-f]{16}"' <<<"$page"
grep -Eq 'data-aster-part-c="[0-9a-f]{16}"' <<<"$page"
grep -Eq 'data-aster-part-d="[0-9a-f]{16}"' <<<"$page"
grep -Eq 'data-aster-part-h="[0-9a-f]{16}"' <<<"$page"
grep -Eq 'data-aster-part-a="[0-9a-f]{16}"' <<<"$page"
test "$(grep -o 'data-aster-component="IsolatedCounter"' <<<"$page" | wc -l)" -eq 2
test "$(grep -o 'data-aster-component="SeededCounter"' <<<"$page" | wc -l)" -eq 2
grep -q 'data-aster-component-param-0="s"' <<<"$page"
grep -q 'data-aster-component-arg-0="Alpha"' <<<"$page"
grep -q 'data-aster-component-param-1="l"' <<<"$page"
grep -q 'data-aster-component-arg-1="40"' <<<"$page"
grep -q 'data-aster-component-param-2="b"' <<<"$page"
grep -q 'data-aster-component-arg-2' <<<"$page"
grep -q 'click|IsolatedCounter_Increment|l|x:IsolatedCounter|l:count' <<<"$page"
test "$(grep -o 'name="value"' <<<"$page" | wc -l)" -ge 2
grep -q 'name="positive"' <<<"$page"
grep -q 'name="canDecrease"' <<<"$page"
grep -q 'data-aster-event="input|ProjectQuery|a|s:query"' <<<"$page"
grep -q 'data-aster-event="input|ProjectQueryLater|A|s:query"' <<<"$page"
grep -q 'data-aster-event="click|RemoveTodo|r|s:key"' <<<"$page"
test "$(curl -fsS \
    "http://127.0.0.1:${port}/browser/browser_http_server.wasm" |
    wc -c)" -gt 0
curl -fsS "http://127.0.0.1:${port}/browser/aster.js" |
    grep -q 'hydrateAster'
loader=$(curl -fsS \
    "http://127.0.0.1:${port}/browser/browser_http_server.js")
grep -q 'browser_http_server.wasm' <<<"$loader"
grep -q 'new URL' <<<"$loader"
curl -fsS -X POST \
    -H 'Content-Type: application/x-www-form-urlencoded' \
    --data 'name=Brandon' \
    "http://127.0.0.1:${port}/contact" |
    grep -q 'Saved without WebAssembly'

wait "$server_pid"
server_pid=""
