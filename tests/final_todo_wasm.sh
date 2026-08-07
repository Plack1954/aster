#!/usr/bin/env bash
set -euo pipefail

lang=$1
output_directory=$2

rm -rf "$output_directory"
"$lang" project build-web \
    packages/aster_web/FinalTodoProof.asproj "$output_directory"
python3 tests/final_todo_browser.py "$lang" "$output_directory"
