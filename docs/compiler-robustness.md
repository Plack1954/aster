# Continuous compiler validation

Aster treats strict compilers, sanitizers, coverage-guided input variation,
and ownership regression tests as normal development gates rather than
occasional release exercises.
The repository workflow runs the complete test inventory with strict GCC and
Clang, then repeats it with ASan and UBSan under both compilers. Persistent
project caching is disabled in these jobs so compilation paths are exercised.

Local equivalents are:

```sh
cmake -S . -B build-gcc -DCMAKE_C_COMPILER=gcc \
  -DLANG_WARNINGS_AS_ERRORS=ON -DASTER_GENERATED_C_STRICT=ON
cmake --build build-gcc
ASTER_CACHE_DIR= ctest --test-dir build-gcc --output-on-failure

cmake -S . -B build-clang -DCMAKE_C_COMPILER=clang \
  -DLANG_WARNINGS_AS_ERRORS=ON -DASTER_GENERATED_C_STRICT=ON
cmake --build build-clang
ASTER_CACHE_DIR= ctest --test-dir build-clang --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_C_COMPILER=clang \
  -DLANG_WARNINGS_AS_ERRORS=ON -DLANG_SANITIZE=ON
cmake --build build-sanitize
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ASTER_CACHE_DIR= \
ctest --test-dir build-sanitize --output-on-failure
```

Generated-C warning analysis belongs to the strict GCC/Clang jobs above.
Sanitizer jobs compile the same generated programs with runtime checking but
do not repeat the expensive generated-source warning analysis.

## Input robustness targets

Clang builds five coverage-guided robustness entry points:

| Target | Boundary |
| --- | --- |
| `aster_parser_robustness` | Varied source bytes through parsing and recovery |
| `aster_checker_robustness` | Successfully parsed source through type/flow checking |
| `aster_ir_verifier_robustness` | Valid lowered IR with bounded structural variation through the independent verifier |
| `aster_ownership_robustness` | Checked source through ownership lowering and verification |
| `aster_bytecode_robustness` | Varied instruction words and metadata through bytecode verification |

Build and run the seeded input checks with:

```sh
cmake -S . -B build-robustness -DCMAKE_C_COMPILER=clang \
  -DASTER_BUILD_ROBUSTNESS_TESTS=ON -DLANG_WARNINGS_AS_ERRORS=ON
cmake --build build-robustness --target \
  aster_parser_robustness aster_checker_robustness \
  aster_ir_verifier_robustness aster_ownership_robustness \
  aster_bytecode_robustness
ctest --test-dir build-robustness \
  -L input-robustness --output-on-failure
```

The smoke tests keep ASan and UBSan enabled but omit a duplicate leak scan;
the complete sanitizer jobs already run LeakSanitizer across the entire test
inventory. Per-coverage function printing is also disabled to keep LLVM
symbolization out of the hot input-variation loop. Invalid behavior still
fails the test and can be reproduced locally with full symbolization.

The scheduled workflow additionally gives each target a 60-second input
variation pass and uploads any reproducer input. Pull requests use the
deterministic 256-run smoke gate so ordinary validation remains bounded.

For a longer local pass, invoke any target directly with its input directory
and a time budget, for example:

```sh
build-robustness/aster_ownership_robustness \
  -max_total_time=3600 tests/robustness/inputs/ownership
```

Reproducer inputs must be reduced to a stable regression before the issue is
considered closed. Seed inputs are ordinary repository files and should be
kept small enough for continuous smoke runs.

## Ownership regression matrix

The `ownership-regression` CTest label collects tests by invariant rather
than by feature. It runs in its own sanitizer CI job and is also included in
every complete suite.

| Historical regression class | Regression coverage |
| --- | --- |
| Same owner consumed twice; missed copy for the first use | `noncopyable_repeated_argument_rejected`, `automatic_last_use_copy_*` |
| Earlier borrow remains live while a later argument consumes the owner | `automatic_last_use_copy_*`, `ref_aliasing_*` |
| Whole-owner and field/index projections incorrectly treated as disjoint | `projection_moves_*`, `ensure_field_move_whole_use_rejected`, `ensure_dynamic_index_alias_rejected` |
| Repeated or unproven partial move | `noncopyable_projection_moves_*`, `ownership_projection_verifier` |
| Exact and overlapping self-assignment destroys its source | `exact_self_assignment_*` |
| Hidden lifetime extension through async frames | `async_frame_liveness_generated_c_execution` |
| Ownership convention lost through function values/call indirection | `exceptions_function_value_*`, `borrowed_function_values_run` |
| Backend pass-by-address accidentally aliases a required value copy | `ref_parameter_value_copy_*`, `value_copy_isolation_*` |
| Copy throws after partially constructing an argument or aggregate | `throwing_copy_*`, `list_callback_throwing_copy_*` |
| Mutating collection callback copies, moves, or drops the wrong element | `list_callback_custom_copy_*`, `list_callback_throwing_copy_*` |
| Return, break, catch, or exception transfer misses/doubles cleanup | `c_backend_cleanup_*_leak_checked` |

Run only this gate with:

```sh
ctest --test-dir build-sanitize \
  -L ownership-regression --output-on-failure
```
