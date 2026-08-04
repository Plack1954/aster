#!/usr/bin/env bash
set -euo pipefail

aster_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
dependency_root=${ASTER_DEPENDENCY_ROOT:-"${aster_root}/build/dependencies"}
h2o_source="${dependency_root}/h2o"
h2o_build="${dependency_root}/h2o-build"
aster_build=${ASTER_H2O_BUILD:-"${aster_root}/build/h2o"}
h2o_commit=706842c0f8c0d9422efb97a4d8ef7d6ec9df87b7

mkdir -p "${dependency_root}"
if [[ ! -d "${h2o_source}/.git" ]]; then
    git clone --filter=blob:none https://github.com/h2o/h2o.git "${h2o_source}"
fi
git -C "${h2o_source}" fetch --depth=1 origin "${h2o_commit}"
git -C "${h2o_source}" checkout --detach "${h2o_commit}"

cmake -S "${h2o_source}" -B "${h2o_build}" \
    -DBUILD_SHARED_LIBS=ON \
    -DWITH_MRUBY=OFF \
    -DDISABLE_LIBUV=ON
cmake --build "${h2o_build}" --target libh2o-evloop -j2

cmake -S "${aster_root}" -B "${aster_build}" \
    -DASTER_H2O_SOURCE_ROOT="${h2o_source}" \
    -DASTER_H2O_BUILD_ROOT="${h2o_build}"
cmake --build "${aster_build}" -j2

printf '%s\n' "H2O-enabled Aster: ${aster_build}/lang"
