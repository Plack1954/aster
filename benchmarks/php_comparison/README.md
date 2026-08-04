# PHP comparison benchmarks

These workloads compare Aster with PHP on a deliberately narrow application
surface: checked integer logic, function calls, owned string construction,
escaped typed HTML, and a synchronous HTTP baseline.

Run all workloads from the Aster repository root:

```sh
./benchmarks/php_comparison/run.sh
```

The runner requires PHP, CMake, a C17 compiler, Hyperfine, ApacheBench, Curl,
and jq. It builds Aster and generated C in a fresh release directory, checks
that both language implementations emit identical output, then stores raw
Hyperfine JSON and environment metadata beneath
`build-php-comparison/results/`.

## Compared execution paths

- `aster-c`: Aster's primary deployment path, emitted as C17 and compiled
  with `-O2 -DNDEBUG`;
- `php`: the installed PHP CLI with its default JIT policy;
- `aster-vm`: Aster's typed-IR bytecode development path.

Each CLI timing includes process startup. The workloads are long enough that
startup is a small portion of logic and function timing and a still-visible
part of the allocation-heavy workloads. No result should be presented as a
general language-speed claim.

## Workload contract

- `logic` executes the same twenty-million-iteration checked recurrence.
- `functions` executes ten million calls to the same recurrence function.
- `strings` constructs 300,000 equivalent owned records containing integer
  and boolean formatting and sums their byte lengths.
- `html` renders 200,000 equivalent cards using Aster element syntax and
  PHP's native `?>...<?php` template syntax with output buffering. Dynamic
  text is escaped in both implementations, and both materialize one final
  owned page string.
- `http` renders the same dynamic HTML response on every request. ApacheBench
  uses one request per connection and concurrency one by default, matching
  Aster's current synchronous server design.

The HTTP comparison uses PHP's single-process built-in development server
because PHP-FPM is not part of the repository and may not be installed. It is
therefore a local server-stack baseline, not a claim about PHP-FPM, nginx,
Apache modules, or production capacity. Aster's HTTP implementation is also
experimental. Override the request count with `HTTP_REQUESTS` and the measured
run count with `HTTP_RUNS`.
