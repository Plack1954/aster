# Aster thesis

Aster is a deterministic application language with explicit costs, simple
cleanup, ordinary value semantics, first-class typed HTML, excellent generated C, and a fast bytecode
VM development loop.

It is intended for production-shaped command-line and synchronous server
applications whose authors want more control and predictability than a
garbage-collected application language without adopting Rust's lifetime model
or building directly in C.

## Design lineage

Aster belongs to the C family, but it is not committed exclusively to C#
syntax. It may adopt syntax and semantic tools from C, C++, or C# when they
produce the clearest Aster design:

- C# and .NET guide the application-facing programming model, public library,
  naming, overloads, nullable values, exceptions, delegates, and tasks;
- C guides implementation, portable deployment, ABI boundaries, raw pointers,
  and direct interoperability;
- C++ is a valid influence for deterministic destruction, RAII, ordinary value
  semantics, explicit object lifetime, copy control, non-owning references, and
  related syntax.

These influences are guides rather than compatibility promises. Aster can use
C or C++ syntax where it is clearer than a C# spelling, and it does not reject
a design merely because it resembles C or C++.

Rust is not an inspiration for Aster and is not a direction in which the
language is intended to converge. Aster does not adopt Rust's borrow checker,
lifetime annotations, move-only-by-default values, ownership types, or
ownership ceremony. Similarities in isolated, widely used features do not
imply Rust lineage. Aster follows the C and C++ trust boundary instead:
deterministic copying and destruction are language concerns, while the
validity and lifetime of pointers, references, aliases, and borrowed views
remain the programmer's responsibility.

## What must be distinctive

Aster should make these properties true in ordinary application code:

- destruction is deterministic and does not depend on a tracing collector;
- immutable strings and shared native handles use narrow reference counting;
- ordinary assignment and calls copy values without invalidating their source;
- allocation, noncopyable resources, and unsafe operations remain visible;
- common read-only operations do not duplicate storage;
- typed HTML, routing, validation, SQLite, configuration, and file processing
  compose as ordinary Aster libraries;
- generated C is portable, warning-clean, inspectable, and straightforward to
  embed or link with established C libraries;
- the VM provides a quick edit-run-test cycle with the same language semantics
  as generated C;
- typed IR contains copy, cleanup, and cost decisions before either backend sees a
  program.

Aster does not need to match the feature breadth of V, Go, Rust, or a mature
web ecosystem. It must make this narrower combination unusually coherent.

## Backend priority

Backend work is prioritized in this order:

1. **Portable C17.** This is the main deployment, portability, performance,
   inspection, and interoperability path.
2. **Bytecode VM.** This is the development, diagnostics, embedding, and
   differential-testing path.
Both paths consume verified typed IR. Backend priority may affect coverage and
scheduling, never source-language meaning.

## Feature gate

A proposed language or library feature should answer all of these questions:

1. What real Aster program is difficult without it?
2. What does it allocate, clone, retain, dispatch dynamically, or clean up?
3. Can those costs be stated locally and lowered explicitly into typed IR?
4. Can generated C implement it correctly and predictably?
5. Can the VM preserve the same observable copy and cleanup behavior?
6. Is the narrow implementation sufficient, or is it introducing speculative
   generality?

A feature should normally wait when there is no pressure-test program, its
cost model is implicit, or its implementation primarily exists to increase
language feature count.

## Application proof

The principal design tests are substantial applications written primarily in
Aster. They should exercise:

- configuration and validation;
- routing and middleware;
- typed HTML components;
- SQLite queries and application models;
- file and directory processing;
- tests and reusable packages;
- deterministic success, error, early-return, and trap cleanup.

For these workloads, the project should periodically record:

- correctness across generated C and the VM;
- generated-C warning cleanliness and sanitizer results;
- allocation and copy behavior at important operations;
- startup time, runtime, peak memory, and output size where meaningful.

Passing small compiler fixtures is necessary but does not establish the
language thesis.

## Non-goals for the current direction

- matching another language feature-for-feature;
- restricting Aster to exclusively C#-derived syntax or rejecting useful C or
  C++ syntax and semantics;
- adopting Rust-inspired ownership, lifetime, borrow-checking, move-only
  defaults, or source-level ownership ceremony;
- adding traits, reflection, or a custom machine-code backend before
  applications demonstrate a concrete need;
- replacing proven C libraries merely to maximize the amount of Aster code;
- claiming production readiness for the current HTTP experiment.

C remains appropriate for operating-system mechanisms, allocator boundaries,
and established libraries. Aster should increasingly own application policy
and reusable APIs when doing so improves the language and its programs.
