#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
    printf 'usage: %s MANIFEST TARGET OUTPUT_DIRECTORY [BINARY_NAME]\n' "$0" >&2
    exit 2
fi

aster_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
manifest=$1
target=$2
output_directory=$3
binary_name=${4:-server}
aster_build=${ASTER_H2O_BUILD:-"${aster_root}/build/h2o"}
dependency_root=${ASTER_DEPENDENCY_ROOT:-"${aster_root}/build/dependencies"}
h2o_build=${ASTER_H2O_LIBRARY_ROOT:-"${dependency_root}/h2o-build"}
c_compiler=${CC:-cc}

if [[ ! -f "${manifest}" ]]; then
    printf 'manifest does not exist: %s\n' "${manifest}" >&2
    exit 1
fi
if [[ "${binary_name}" == */* || -z "${binary_name}" ]]; then
    printf 'binary name must be one non-empty path component\n' >&2
    exit 2
fi
if [[ ! -x "${aster_build}/lang" ||
      ! -f "${aster_build}/liblanglib.a" ||
      ! -f "${h2o_build}/libh2o-evloop.so" ||
      ! -f "${h2o_build}/libh2o-evloop.so.0.16" ]]; then
    printf 'build the pinned H2O adapter first: %s/tools/build_h2o.sh\n' \
        "${aster_root}" >&2
    exit 1
fi

mkdir -p "${output_directory}/lib"
generated_c="${output_directory}/${binary_name}.c"
next_binary="${output_directory}/${binary_name}.next"
final_binary="${output_directory}/${binary_name}"

(cd "${aster_root}" &&
    "${aster_build}/lang" project emit-c "${manifest}" "${target}") \
    > "${generated_c}"

"${c_compiler}" \
    -std=c17 -O2 -DNDEBUG \
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -I "${aster_root}/include" \
    "${generated_c}" \
    "${aster_build}/liblanglib.a" \
    -lsqlite3 "${h2o_build}/libh2o-evloop.so" \
    '-Wl,-rpath,$ORIGIN/lib' \
    -o "${next_binary}"

cp -L "${h2o_build}/libh2o-evloop.so.0.16" \
    "${output_directory}/lib/libh2o-evloop.so.0.16"
mv -f "${next_binary}" "${final_binary}"

printf 'built Lime/H2O bundle: %s\n' "${final_binary}"
printf 'runtime library: %s\n' \
    "${output_directory}/lib/libh2o-evloop.so.0.16"
