#!/usr/bin/env bash
set -euo pipefail

lang=$1
c_compiler=$2
runtime_library=$3
crypto_library=$4
sqlite_library=$5
openssl_library=$6
output_directory=$7

mkdir -p "$output_directory"
"$lang" run tests/cryptography_surface.as
"$lang" emit-c tests/cryptography_surface.as > "$output_directory/crypto.c"

libraries=("$crypto_library" "$runtime_library")
if [[ -n "$sqlite_library" ]]; then libraries+=("$sqlite_library"); fi
if [[ -n "$openssl_library" ]]; then libraries+=("$openssl_library"); fi
"$c_compiler" \
    -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror \
    -I include "$output_directory/crypto.c" "${libraries[@]}" \
    -o "$output_directory/crypto"
"$output_directory/crypto"
