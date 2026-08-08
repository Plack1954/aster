#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import statistics
import subprocess
import time

ITERATIONS = 250_000
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = HERE / "target"


def run(command, *, cwd=ROOT, env=None, capture=False):
    return subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
    )


def build():
    OUT.mkdir(exist_ok=True)
    compiler = ROOT / "build" / "lang"
    if not compiler.exists():
        raise SystemExit("build/lang is missing; build Aster first")

    binaries = {}
    for mode in ("escaped", "raw"):
        source = HERE / f"aster_{mode}.as"
        generated = OUT / f"aster_{mode}.c"
        emitted = run([str(compiler), "emit-c", str(source)], capture=True)
        generated.write_text(emitted.stdout, encoding="utf-8")
        binary = OUT / f"aster_{mode}"
        run(["cc", "-std=c17", "-O3", "-DNDEBUG", "-march=native",
             str(generated), "-o", str(binary)])
        binaries[("Aster", mode)] = [str(binary)]

    env = os.environ.copy()
    env["RUSTFLAGS"] = "-C target-cpu=native"
    run(["cargo", "build", "--release"], cwd=HERE / "sailfish", env=env)
    sailfish = HERE / "sailfish" / "target" / "release" / "aster-sailfish-benchmark"
    for mode in ("escaped", "raw"):
        binaries[("Sailfish", mode)] = [str(sailfish), mode]
    return binaries


def sample(command, count):
    values = []
    expected = None
    for _ in range(count):
        begin = time.perf_counter_ns()
        completed = run(command, capture=True)
        elapsed = time.perf_counter_ns() - begin
        output = completed.stdout.strip()
        if not output.isdigit():
            raise SystemExit(f"unexpected benchmark output: {output!r}")
        if expected is None:
            expected = output
        elif output != expected:
            raise SystemExit("benchmark byte count changed between samples")
        values.append(elapsed)
    return statistics.median(values), int(expected)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", type=int, default=7)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("--samples must be positive")

    binaries = build()
    results = {}
    for mode in ("escaped", "raw"):
        for engine in ("Aster", "Sailfish"):
            elapsed, byte_count = sample(binaries[(engine, mode)], args.samples)
            results[(engine, mode)] = elapsed
            ns_per_render = elapsed / ITERATIONS
            rate = 1e9 / ns_per_render
            print(f"{engine:8} {mode:7} {ns_per_render:9.1f} ns/render "
                  f"{rate:12,.0f} renders/s  bytes={byte_count}")
        ratio = results[("Sailfish", mode)] / results[("Aster", mode)]
        winner = "Aster" if ratio > 1 else "Sailfish"
        factor = ratio if ratio > 1 else 1 / ratio
        print(f"{mode:7} winner: {winner} ({factor:.2f}x)\n")


if __name__ == "__main__":
    main()
